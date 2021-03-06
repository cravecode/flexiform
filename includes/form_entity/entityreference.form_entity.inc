<?php
/**
 * @file
 *  Contains class for a basic entity getter.
 */

/**
 * Form Entity Class for Managing the Entity Reference fields
 */
class FlexiformFormEntityEntityReference extends FlexiformFormEntityBase {

  /**
   * {@inheritdoc}
   */
  public function getEntity() {
    parent::getEntity();

    $settings = $this->settings;

    // Get the base entity.
    $base_entity = $this->getParam('base');

    // Get the delta.
    $delta = 0;
    if (!empty($settings['delta'])) {
      $delta = $settings['delta'];
    }

    // Check we have enough information to load the entity.
    if (!$base_entity || empty($base_entity->{$this->getter['field_name']}[LANGUAGE_NONE][$delta]['target_id'])) {
      return empty($base_entity) ? FALSE : $this->createEntity();
    }

    // Load the entity;
    $target_id = $base_entity->{$this->getter['field_name']}[LANGUAGE_NONE][$delta]['target_id'];
    $target_entity = entity_load_single($this->entity_type, $target_id);

    if (!$target_entity) {
      return FALSE;
    }

    return $this->checkBundle($target_entity) ? $target_entity : FALSE;
  }

  /**
   * Create an entity if the settings allow it.
   */
  protected function createEntity() {
    if (empty($this->settings['create'])) {
      return FALSE;
    }

    $entity_info = entity_get_info($this->entity_type);
    $values = array();
    if (!empty($entity_info['entity keys']['bundle'])) {
      $values[$entity_info['entity keys']['bundle']] = $this->settings['bundle'];
    }

    return entity_create($this->entity_type, $values);
  }

  /**
   * {@inheritdoc}
   */
  public function saveEntity($entity) {
    // If the entity is still false do not save it!
    if ($entity === FALSE) {
      return;
    }

    // Save the entity.
    entity_save($this->entity_type, $entity);
    list($id,,) = entity_extract_ids($this->entity_type, $entity);

    // Get Settings for this Getter/Setter.
    $settings = $this->settings;

    // Get the delta.
    $delta = 0;
    if (!empty($settings['delta'])) {
      $delta = $settings['delta'];
    }

    // Get the base entity and save the reference back.
    $base_entity = $this->getParam('base');
    if (empty($base_entity->{$this->getter['field_name']}[LANGUAGE_NONE][$delta]['target_id'])
      || $base_entity->{$this->getter['field_name']}[LANGUAGE_NONE][$delta]['target_id'] != $id) {
      $base_entity->{$this->getter['field_name']}[LANGUAGE_NONE][$delta]['target_id'] = $id;
      $base_entity_type = $this->getParamType('base');
      entity_save($base_entity_type, $base_entity);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function configForm($form, &$form_state) {
    $form = parent::configForm($form, $form_state);
    $form['settings'] = array(
      '#type' => 'container',
      '#tree' => TRUE,
    );

    $field = field_info_field($this->getter['field_name']);

    // Allow the selection of which one to use.
    if ($field['cardinality'] != 1) {

      $options = array();
      $i = 0;
      while ($i < 10) {
        if ($field['cardinality'] > 0 && $i >= $field['cardinality']) {
          break;
        }

        $options[$i] = $i + 1;
        ++$i;
      }
      $form['settings']['delta'] = array(
        '#title' => t('Delta'),
        '#description' => t('Select which value of this field you want to use.'),
        '#type' => 'select',
        '#options' => $options,
        '#default_value' => !empty($this->settings['delta']) ? $this->settings['delta'] : 0,
      );
    }

    $form['settings']['create'] = array(
      '#title' => t('Create if Empty'),
      '#description' => t('When ticked, if the field is empty, a new entity will be created.'),
      '#type' => 'checkbox',
      '#default_value' => !empty($this->settings['create']),
    );

    return $form;
  }
}
