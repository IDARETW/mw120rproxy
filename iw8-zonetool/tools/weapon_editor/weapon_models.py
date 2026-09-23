"""Classify WeaponDef model relocations used to draw the weapon itself."""
import re

from graph import walk


VIEW_MODEL_FIELDS = (
    'gunXModel', 'gunXModelLeftHand', 'gunXModelRightHand', 'defaultViewModel',
)
WORLD_MODEL_FIELDS = (
    'worldModel', 'worldXModelLeftHand', 'worldXModelRightHand',
    'defaultWorldModel', 'defaultWorldModelLeftHand', 'defaultWorldModelRightHand',
    'censorshipWorldModel', 'censorshipWorldModelLeftHand',
    'censorshipWorldModelRightHand',
)


def _model_name(value):
    if isinstance(value, dict):
        value = value.get('name', '')
    return str(value or '').lstrip(',')


def _append_unique(target, name):
    if name and name not in target:
        target.append(name)


def model_slots_from_definition(definition):
    """Read view/world XModels from a decoded native WeaponDef."""
    result = {'view_model': [], 'world_model': []}
    for field in VIEW_MODEL_FIELDS:
        _append_unique(result['view_model'], _model_name(definition.get(field)))
    for field in WORLD_MODEL_FIELDS:
        _append_unique(result['world_model'], _model_name(definition.get(field)))
    return result


def model_slots_from_record(root):
    """Read view/world XModel fixups from a prepared WeaponCompleteDef graph."""
    result = {'view_model': [], 'world_model': []}
    for _, fixup in walk(root):
        if fixup.get('kind') != 'asset' or fixup.get('asset_type') != 9:
            continue
        match = re.fullmatch(r'weapon\.weapDef\[\d+\]\.([A-Za-z0-9_]+)',
                             str(fixup.get('field', '')))
        if not match:
            continue
        field = match.group(1)
        target = ('view_model' if field in VIEW_MODEL_FIELDS else
                  'world_model' if field in WORLD_MODEL_FIELDS else None)
        if target:
            _append_unique(result[target], fixup.get('name', ''))
    return result
