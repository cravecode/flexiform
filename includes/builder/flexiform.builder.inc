<?php
/**
 * @file
 * Contains the FlexiformBuilderFlexiform Class
 */

/**
 * Flexiform Builder Class for the complex flexiforms.
 */
class FlexiformBuilderFlexiform extends FlexiformBuilder {

  /**
   * The Entity Manager for this form.
   * @var FlexiformFormEntityManagerInterface
   */
  protected $entityManager;

  /**
   * The Flexiform State for this form.
   * @var Array
   */
  protected $flexiformState;

  /**
   * Get the entity manager.
   *
   * @return FlexiformFormEntityManagerInterface
   *   The entity manager for this form builder.
   */
  public function getEntityManager(&$state = array()) {
    if (!empty($this->entityManager)) {
      return $this->entityManager;
    }

    if (!empty($state['flexiform_entity_manager'])) {
      $this->entityManager = $state['flexiform_entity_manager'];
      return $this->entityManager;
    }

    $this->entityManager = new FlexiformFormEntityManagerDefault($this->flexiform->entities, $this);
    $this->entityManager->setBaseEntity($this->base_entity);
    $state['flexiform_entity_manager'] = $this->entityManager;
    return $this->entityManager;
  }

  /**
   * Set a entity callback.
   */
  public function registerEntityCallback($type, $namespace, $callback, $extra = array(), &$state = array()) {
    $this->getEntityManager($state)->registerCallback($type, $namespace, $callback, $extra);
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
    return $this->getEntityManager($state)->getEntity($namespace);
  }

  /**
   * Get the flexiform state.
   *
   * @param array $form
   *   The form array being built. Must have a parents key.
   * @param array $form_state
   *   The form_State array.
   *
   * @return array
   *   The flexiform state.
   */
  public function &getFlexiformState($form, &$form_state) {
    // Shortcut if we've already got the array.
    if (is_array($this->flexiformState)) {
      return $this->flexiformState;
    }

    // Set up a place to safely store information in the form state.
    if (empty($form_state['flexiform_state'])) {
      $form_state['flexiform_state'] = array();
    }

    // Get the flexiform state for this particular form. This is a bit involved
    // as we need to be able to nest flexiforms.
    $this->flexiformState = drupal_array_get_nested_value($form_state['flexiform_state'], $form['#parents']);
    if (empty($this->flexiformState)) {
      $this->flexiformState = array();
    }

    return $this->flexiformState;
  }

  /**
   * Set the flexiform state.
   *
   * @param array $form
   *   The form array being built. Must have a parents key.
   * @param array $form_state
   *   The form_State array.
   */
  public function setFlexiformState($form, &$form_state) {
    // Set up a place to safely store information in the form state.
    if (empty($form_state['flexiform_state'])) {
      $form_state['flexiform_state'] = array();
    }

    // Set the flexiform state.
    drupal_array_set_nested_value($form_state['flexiform_state'], $form['#parents'], $this->flexiformState);
  }

  /**
   * Build the form for this flexiform.
   */
  public function form($form, &$form_state) {
    $form = parent::form($form, $form_state);

    // Set up our form bits
    if (!isset($form['#parents'])) {
      $form['#parents'] = array();
    }

    // Get the flexiform_state
    $flexiform_state = &$this->getFlexiformState($form, $form_state);
    // Get the form entities and store the manager in the flexiform state.
    $form['#flexiform_entities'] = $this->getEntityManager($flexiform_state)->getEntities();
    $this->setFlexiformState($form, $form_state);

    // Iterate over all elements.
    foreach ($this->flexiform->elements as $element_namespace => $settings) {
      $element = FlexiformElement::getElement($this->flexiform, $element_namespace);

      // Don't show form elements for entities that don't exist.
      if (!$this->getFormEntity($element->getEntityNamespace(), $flexiform_state)) {
        watchdog('flexiform', 'The "@element" element\'s entity is not gettable, so nothing to show for it on the form.', array('@element' => $element->getElementNamespace()), WATCHDOG_WARNING);
        continue;
      }

      // Make sure this element has the correct parents.
      array_push($form['#parents'], $element->getEntityNamespace());
      $form += $element->form($form, $form_state, $this->getFormEntity($element->getEntityNamespace(), $flexiform_state));
      array_pop($form['#parents']);
    }

    $this->invoke($form, $form_state);

    return $form;
  }

  /**
   * Validate the input for the form.
   */
  public function formValidate($form, &$form_state) {
    // Get the flexiform state for this particular form.
    $flexiform_state = &$this->getFlexiformState($form, $form_state);


    foreach (element_children($form) as $element_namespace) {
      if (empty($form[$element_namespace]['#flexiform_element'])) {
        continue;
      }

      $element = $form[$element_namespace]['#flexiform_element'];
      $element->formValidate($form, $form_state, $this->entityValidationCache($element->getEntityNamespace(), $flexiform_state));
    }

    $errors = array();
    // Validate the form in the context of each entity.
    foreach($this->flexiform->entities as $name_space => $entity_settings) {

      // Let other modules validate the entity.
      foreach (module_implements('field_attach_validate') as $module) {
        $function = $module . '_field_attach_validate';
        $entity = $this->entityValidationCache($name_space, $flexiform_state);
        if($entity) {
          $function($entity_settings['entity_type'], $entity, $errors);
          // Because Field Validate uses form_set_error we don't actually
          // end up doing anything with "$errors".
          // In my tests, it works without manually writing them to the page.
        }
      }
    }

    $this->invoke($form, $form_state, 'validate');

    // Grab all errors created by form_set_error and
    //  * sort them based on element position
    //  * correct any incorrectly set element paths for errors.
    //  * Remove any duplicates messages
    $this->updateFormErrorMessages(form_get_errors(), $form, $form_state);

    $this->invoke($form, $form_state, 'validate');
  }

  /**
   * Grabs and stashes form entities via a static cache.
   * This is intended to be performed within the scope of the form validation process.
   * Each entity retrieval from the FlexiformBuilder is cloned to avoid
   * entity changes from leaking outside of the validation processes.
   *
   * @param string $entity_namespace
   * @param array $flexiform_state
   * @return bool|\Entity|\stdClass
   */
  private function entityValidationCache($entity_namespace, $flexiform_state) {
    static $entity_cache = array();

    // Setup a entity cache so that validation steps are carried across multiple
    // validation rules without globally altering the entities outside the scope of validation.
    if(key_exists($entity_namespace, $entity_cache)) {
      $entity = $entity_cache[$entity_namespace];
    }
    else {
      // We don't have a cached version of the entity yet, grab it and stash to cache.
      $entity = $this->getFormEntity($entity_namespace, $flexiform_state);
      // Clone the entity so we're not altering the entity outside of the validation process.
      $entity = clone $entity;
      $entity_cache[$entity_namespace] = $entity;
    }

    return $entity;
  }

  /**
   * Submit the form.
   */
  public function formSubmit($form, &$form_state) {
    $flexiform = $form['#flexiform'];
    $flexiform_state = &$this->getFlexiformState($form, $form_state);

    foreach (element_children($form) as $element_namespace) {
      if (empty($form[$element_namespace]['#flexiform_element'])) {
        continue;
      }

      $element = $form[$element_namespace]['#flexiform_element'];
      $entity = $this->getFormEntity($element->getEntityNamespace(), $flexiform_state);
      $element->formSubmit($form, $form_state, $entity);
    }

    $this->invoke($form, $form_state, 'submit');

    $this->getEntityManager($flexiform_state)->saveEntities();

    if (module_exists('rules')) {
      $base_entity_wrapper = entity_metadata_wrapper($flexiform->base_entity, $form['#flexiform_base_entity']);
      $args = array(
        'base_entity' => $base_entity_wrapper,
      );

      // All Arguments: Rules needs nulls as null rather than false.
      $all_args = $form['#flexiform_entities'];
      foreach ($all_args as &$ent) {
        if (empty($ent)) {
          $ent = NULL;
        }
      }

      // Fire the most specific rules first and track back to the less specific.
      $rule_name = 'flexiform_submit_'.$this->flexiform->form;
      drupal_alter('flexiform_rules_arguments', $all_args, $form_state, $rule_name);
      rules_invoke_event_by_args('flexiform_submit_'.$this->flexiform->form, $all_args);
      $rule_name = 'flexiform_group_submit_'.$this->flexiform->form_group;
      drupal_alter('flexiform_rules_arguments', $args, $form_state, $rule_name);
      rules_invoke_event_by_args('flexiform_group_submit_'.$this->flexiform->form_group, $args);
      $rule_name = 'flexiform_any_submit';
      drupal_alter('flexiform_rules_arguments', $args, $form_state, $rule_name);
      rules_invoke_event_by_args('flexiform_any_submit', $args);
    }

    $this->setFlexiformState($form, $form_state);
  }

  /**
   * {@inheritdoc}
   */
  public function supportsComponent($component) {
    return in_array($component, array('entities', 'elements'));
  }
}
