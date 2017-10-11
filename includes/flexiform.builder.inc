<?php
/**
 * @file
 * Specify builder classes for flexiforms.
 */

/**
 * Interface for all Flexiform Builders.
 */
interface FlexiformBuilderInterface {

  /**
   * Build the form for this flexiform.
   */
  public function form($form, &$form_state);

  /**
   * Validate the input for the form.
   */
  public function formValidate($form, &$form_state);

  /**
   * Submit the form.
   */
  public function formSubmit($form, &$form_state);

  /**
   * Support Components.
   *
   * Determine whether or not a builder supports a paricular component.
   */
  public function supportsComponent($component);
}

/**
 * Base class for flexiform builders.
 */
class FlexiformBuilder implements FlexiformBuilderInterface {

  /**
   * The flexiform object.
   */
  protected $flexiform;

  /**
   * The base entity.
   */
  protected $base_entity;

  /**
   * The base entity type.
   */
  protected $base_entity_type;

  /**
   * Construct the Flexiform Builder.
   */
  public function __construct($flexiform, $base_entity) {
    $this->flexiform = $flexiform;
    $this->base_entity = $base_entity;
    $this->base_entity_type = $flexiform->base_entity;
  }

  /**
   * Build the form for this flexiform.
   */
  public function form($form, &$form_state) {
    $form['#flexiform'] = $this->flexiform;
    $form['#flexiform_base_entity'] = $this->base_entity;
    $form['#flexiform_builder'] = $this;
    $form['#theme'] = array('flexiform__'.$this->flexiform->form, 'flexiform');
    $form['#contextual_links']['flexiform'] = array('admin/structure/flexiforms/manage', array($this->flexiform->form));

    return $form;
  }

  /**
   * Validate the input for the form.
   */
  public function formValidate($form, &$form_state) {}

  /**
   * Fix generated form errors so that they map to the correct form element
   * and are in the correct order.
   *
   * @param array $form_errors Existing form errors. (I.e.: form_get_errors()).
   */
  protected function updateFormErrorMessages($form_errors, $form, $form_state) {
    // Correct validation error message element references.
    $form_errors = $this->fixFormErrorElementPaths($form_errors, $form, $form_state);

    // Order $form_errors by the element weight.
    usort($form_errors, function($a, $b){
      return ($a['weight'] == $b['weight']
        ? 0 :
        ($a['weight'] > $b['weight']
          ? 1
          : -1));
    });

    // Get messages already saved to session to remove any potential duplicates.
    // Pass by reference so we can manipulate them directly.
    $session_messages = &$_SESSION['messages']['error'];

    // Clear existing form errors so we can rebuild them.
    form_clear_error();

    // Loop over our messages that have been re-mapped to their respective
    // form elements and remove any potential duplicates.
    foreach($form_errors as $err_msg) {
      // Has this error message been previously logged to session messages?
      $duplicate_msg_index = array_search($err_msg['message'], $session_messages, TRUE);
      if($duplicate_msg_index !== FALSE) {
        // Found a duplicate, remove it from the message saved in session.
        unset($session_messages[$duplicate_msg_index]);
      }

      // Set our new version of the message that is
      // correctly mapped to a form element.
      form_set_error($err_msg['path'], $err_msg['message']);
    }

    // Because we have potentially the messages index sequence, re-key the
    // array of messages in session.
    $session_messages = array_values($session_messages);
  }


  /**
   * Flexiform doesn't build the form element names using the
   * entity's name, rather the entity's namespace that is defined in
   * Flexiform Entity configuration.
   * We need to change references to entities by name to namespace
   * in form error messages.
   *
   * Example change:
   * From:  node][sample_field][und][0][value
   * To:    base_entity][sample_field][und][0][value
   *
   * @param array $form_errors Existing form errors. (I.e.: form_get_errors())
   * @return array
   */
  protected function fixFormErrorElementPaths($form_errors, $form, $form_state) {
    // Storage for our corrected error messages.
    $new_errors = array();

    $correct_error_element_path = function($error_element_path, $element) {
      $entity_type = $element->getEntityType();
      $namespace = $element->getEntityNamespace();

      // First attempt to replace the entity_type with the entity_namespace
      // If the element name starts with the entity type,
      // replace it with our namespace.
      if(stripos($error_element_path, "$entity_type]") === 0) {
        // Update the element mapping.
        $error_element_path = substr_replace(
          $element, "$namespace]", 0, (count_chars($entity_type) + 1));
      }
      // Then check if entity_namespace is at the beginning, if not, prefix it.
      if(stripos($error_element_path, $namespace) !== 0) {
        $error_element_path = "$namespace][$error_element_path";
      }

      // Return the updated path.
      return $error_element_path;
    };

    $elements = $this->getElementsFromForm($form);

    foreach($form_errors as $error_element_path => $msg) {
      $matched_element = null;
      $new_error = array();

      foreach($elements as $element) {
        if($element['#flexiform_element']->isErrorElementPathMatch($error_element_path)) {
          $matched_element = $element;
          break;
        }
      }

      if($matched_element) {
        $new_error['weight'] = $matched_element['#weight'];
        $new_error['path'] = $correct_error_element_path($error_element_path, $matched_element['#flexiform_element']);
      }
      else {
        $new_error['path'] = $error_element_path;
        $new_error['weight'] = 99;
      }
      $new_error['orig_path'] = $error_element_path;
      $new_error['message'] = $msg;

      $new_errors[] = $new_error;
    }

    return $new_errors;
  }

  protected function getElementsFromForm($form) {
    $elements = array();
    foreach (element_children($form) as $element_namespace) {
      if (!empty($form[$element_namespace]['#flexiform_element'])) {
        $elements[] = $form[$element_namespace];
      }
    }
    return $elements;
  }


  /**
   * Submit the form.
   */
  public function formSubmit($form, &$form_state) {}

  /**
   * Submit the form and set up the redirect.
   */
  public function formSubmitRedirect($form, &$form_state) {
    if (empty($this->flexiform->settings['redirect']['path'])) {
      return;
    }

    $redirect = $this->flexiform->settings['redirect']['path'];
    $form_state['redirect'] = $this->replaceCtoolsSubstitutions($redirect);
  }

  /**
   * Invoke hooks to alter the build of this form.
   */
  public function invoke(&$form, &$form_state, $hook = '') {
    if (!empty($hook)){
      $hooks = array(
        'flexiform_build_' . $hook,
        'flexiform_build_' . $this->flexiform->builder . '_' . $hook,
      );
    }
    else {
      $hooks = array(
        'flexiform_build',
        'flexiform_build_' . $this->flexiform->builder,
      );
    }

    drupal_alter($hooks, $form, $form_state, $this->flexiform);
  }

  /**
   * Get the Flexiform.
   */
  public function getFlexiform() {
    return $this->flexiform;
  }

  /**
   * Get a form entity.
   *
   * @param $namespace
   *  The namespace of the entity required.
   *
   * @return
   *  The entity with that namespace.
   */
  public function getFormEntity($namespace, &$state = array()) {
    return ($namespace == 'base_entity') ? $this->base_entity : FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function supportsComponent($component) {
    return FALSE;
  }

  /**
   * Get an array of ctools context for the flexiform.
   *
   * @return ctools_context[]
   *   An array of ctools contexts.
   */
  public function getCtoolsContexts() {
    global $user;

    ctools_include('context');
    $contexts = array(
      'global' => ctools_context_create('token'),
      'current-user' => ctools_context_create('entity:user', $user),
    );
    $contexts['global']->keyword = 'global';
    $contexts['current-user']->keyword = 'current-user';
    $contexts['current-user']->identifier = t('Logged-in user');
    foreach ($this->flexiform->entities as $namespace => $info) {
      // Attempt to get the entity. Ignore any exceptions.
      try {
        $entity = $this->getFormEntity($namespace);
      }
      catch(Exception $e) {}

      // Create context.
      $type = 'entity:' . $info['entity_type'];
      if (!empty($entity)) {
        $contexts[$namespace] = ctools_context_create($type, $entity);
      }
      else {
        $contexts[$namespace] = ctools_context_create_empty($type);
      }
      $contexts[$namespace]->keyword = $namespace;
      $contexts[$namespace]->identifier = $info['label'];
    }

    return $contexts;
  }

  /**
   * Replace ctools substitutions with their values.
   *
   * @param string $string
   *   The string we want to replace in.
   * @param array $keywords
   *   Optionally provide additional keywords to replace.
   *
   * @return string
   *   $string with it's substitutions replaced.
   */
  public function replaceCtoolsSubstitutions($string, $keywords = array()) {
    $contexts = $this->getCtoolsContexts();
    return ctools_context_keyword_substitute($string, $keywords, $contexts);
  }

  /**
   * Build a list of possible ctools substitutions.
   *
   * @param array $keywords
   *   Optionally provide additional keywords to show.
   *
   * @return array
   *   A render array of substitutions.
   */
  public function getCtoolsSubstitutionsList($keywords = array()) {
    $content = array(
      '#theme' => 'table',
      '#header' => array(t('Keyword'), t('Value')),
      '#rows' => array(),
    );

    foreach ($this->getCtoolsContexts() as $context) {
      foreach (ctools_context_get_converters('%' . check_plain($context->keyword) . ':', $context) as $keyword => $title) {
        $content['#rows'][] = array(
          check_plain($keyword),
          t('@identifier: @title', array('@title' => $title, '@identifier' => $context->identifier)),
        );
      }
    }

    if (count($content['#rows'])) {
      return $content;
    }
    else {
      return array();
    }
  }
}
