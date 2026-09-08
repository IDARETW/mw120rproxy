"""Required runtime entity retained by authored Replay maps."""
import re


def validate_baseline_anchor(entities):
    records=[dict(re.findall(rb'(\d+)\s+"([^"]*)"',body))
             for body in re.findall(rb'\{([^{}]*)\}',entities)]
    if not any(record.get(b'212')==b'script_model' for record in records):
        raise ValueError('Authored map has no runtime script_model baseline anchor; baked props and spawn markers alone leave Replay nextNoDeltaEntity at zero')
