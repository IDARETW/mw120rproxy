"""Resolve the complete CoD4 scripted-window state chain before baking models."""
def states(entities):
    by_target={e['targetname']:e for e in entities if e.get('targetname')}
    hidden=set();intact={}
    for trigger in entities:
        if trigger.get('targetname')!='windtrig':continue
        state=by_target.get(trigger.get('target'));seen=set();first=True
        while state is not None:
            key=id(state)
            if key in seen:raise ValueError('Cyclic breakable-window target chain')
            seen.add(key)
            if first:intact[key]=len(intact);first=False
            else:hidden.add(key)
            state=by_target.get(state.get('target'))
    return hidden,intact
