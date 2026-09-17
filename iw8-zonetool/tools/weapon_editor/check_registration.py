"""Validate loadout and attachment-table registration for every weapon reference."""
import argparse
import copy
import json
from pathlib import Path

from graph import attachment_slots
from tables import registration


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()

    catalog = json.loads((args.library / 'catalog.json').read_text(encoding='utf-8-sig'))
    tables = json.loads((args.library / 'tables.json').read_text(encoding='utf-8-sig'))
    selectable = [weapon for weapon in catalog['weapons'] if weapon['selectable']]
    if not selectable:
        raise SystemExit('catalog contains no selectable weapon references')
    fallback_by_category = {}
    for weapon in selectable:
        fallback_by_category.setdefault(weapon['category'], weapon['name'])
    attachment_rows = {row[4]: row for row in tables['mp/attachmenttable.csv'] if row[4]}

    results = []
    for index, weapon in enumerate(catalog['weapons']):
        reference = json.loads((args.library / weapon['file']).read_text(encoding='utf-8-sig'))
        slots = attachment_slots(reference['root'])
        project = {
            'base': f'iw8_cw_reg_{index}',
            'reference_name': weapon['name'],
            'category': weapon['category'],
            'loadout_slot': 61 + index % 194,
            'attachment_slots': copy.deepcopy(slots),
            'owned_assets': [],
        }
        if not weapon['selectable']:
            project['loadout_reference'] = fallback_by_category.get(
                weapon['category'], selectable[0]['name'])

        error = None
        custom = None
        try:
            baseline = registration(project, tables)
            # Only clone an attachment already selected by the native loadout
            # map. A weapon can contain internal alternates whose table token
            # intentionally belongs to a different selectable attachment.
            for token, source in baseline['attachment_map'].items():
                row = attachment_rows.get(source)
                position = next(((slot_index, names.index(source))
                                 for slot_index, names in enumerate(project['attachment_slots'])
                                 if source in names), None)
                if row and row[5] and position:
                    custom = f'iw8_cw_reg_attachment_{index}'
                    project['attachment_slots'][position[0]][position[1]] = custom
                    project['owned_assets'].append({
                        'pool': 42,
                        'name': custom,
                        'source': source,
                        'ui': {'title': f'Registration check {index}', 'token': token},
                    })
                    break
            emitted = registration(project, tables)
            if emitted['loadout_reference'] != project.get(
                    'loadout_reference', weapon['name']).removesuffix('_mp') + '_mp':
                raise ValueError('registration returned the wrong loadout reference')
            if len(emitted['tables']) < 2:
                raise ValueError('registration omitted gunsmith progression or variant tables')
            if custom and not any(row['asset'] == custom for row in emitted['attachment_rows']):
                raise ValueError('custom attachment row was not emitted')
            if custom and custom not in emitted['attachment_map'].values():
                raise ValueError('custom attachment was not added to the attachment map')
        except Exception as exc:
            error = str(exc)
        results.append({
            'weapon': weapon['name'],
            'category': weapon['category'],
            'selectable': weapon['selectable'],
            'custom_attachment': bool(custom),
            'loadout_reference': project.get('loadout_reference', weapon['name']),
            'error': error,
        })

    failures = [result for result in results if result['error']]
    summary = {
        'format': 'replay-registration-check-v1',
        'references': len(results),
        'selectable': sum(result['selectable'] for result in results),
        'special': sum(not result['selectable'] for result in results),
        'categories': sorted({result['category'] for result in results}),
        'custom_attachment_rows': sum(result['custom_attachment'] for result in results),
        'passed': len(results) - len(failures),
        'failed': len(failures),
        'status': 'passed' if not failures else 'failed',
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'summary': summary, 'results': results}, indent=2) + '\n',
                        encoding='utf-8')
    print(json.dumps(summary), flush=True)
    for failure in failures:
        print(json.dumps(failure), flush=True)
    if failures:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
