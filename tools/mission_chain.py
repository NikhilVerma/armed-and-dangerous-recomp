"""A level's objective chain from its exported mission (work/remaster/<level>/mission.json), as a short tree.

    python tools/mission_chain.py LEVEL [--all] [--json]

Default: what wins and what fails the level, traced backwards. Each trigger lists its senders (instance id, template,
the event that sends it, game position) and, for senders that wait on something themselves, what they wait on:
a counter's count and its activate trigger, a ticker site's enable trigger, a volume's enable trigger, a barracks'
or AI's activate trigger, the mission's peasant counts. `--all` adds every trigger with its receivers (the flat
graph); `--json` prints the same as JSON. Static data only: what the engine does at runtime is the playtest's job
(the state log's "triggers" field counts the triggers sent).
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ap = argparse.ArgumentParser()
ap.add_argument('level')
ap.add_argument('--all', action='store_true')
ap.add_argument('--json', action='store_true')
a = ap.parse_args()

m = json.load(open(os.path.join(ROOT, 'work', 'remaster', a.level, 'mission.json'), encoding='utf-8'))
inst = {i['id']: i for i in m['instances']}
trig = {int(k): v for k, v in m['triggers'].items()}
mission = m['mission']['resolved']


def pos(p):
    return [round(v) for v in p[:3]] if p else None


def waits_on(i):
    """What instance i needs before it can send: (trigger, why) pairs, plus a note."""
    r = inst[i]['resolved'] if i in inst else {}
    cls = (inst[i].get('class') or '') if i in inst else ''
    out, note = [], ''
    if inst.get(i, {}).get('template') == 'counter':
        note = f"count {r.get('count')}" + (' (repeats)' if r.get('infinite') else '')
        if r.get('receive_trigger_activate'):
            out.append((r['receive_trigger_activate'], 'each +1'))
        if r.get('receive_trigger_deactivate'):
            out.append((r['receive_trigger_deactivate'], 'switched off by'))
    elif 'TickerBomb' in cls:
        note = 'ticker site: hold Use with ticker bombs'
        if r.get('receive_trigger_enable'):
            out.append((r['receive_trigger_enable'], 'enabled by'))
        else:
            note += ', enabled from the start'
    elif cls.startswith('TriggerObject') or inst.get(i, {}).get('template') in ('doormat',):
        v = inst[i].get('volume') or {}
        shape = f"sphere r {v.get('radius')}" if v.get('kind') == 'sphere' else 'box'
        note = f"volume {shape}" + (', AI too' if r.get('ai_can_trigger') else '') + (', one-shot' if r.get('one_shot') else '')
        if r.get('enable'):
            out.append((r['enable'], 'enabled by'))
        if r.get('disable'):
            out.append((r['disable'], 'disabled by'))
    elif 'Timer' in cls or inst.get(i, {}).get('template') == 'timer':
        # send_trigger_time_N = "trigger/seconds after activation"
        times = [v for k, v in r.items() if k.startswith('send_trigger_time_') and isinstance(v, str) and not v.startswith('0/')]
        note = 'timer ' + ', '.join(f"{t.split('/')[0]} at {t.split('/')[1]} s" for t in times)
        if r.get('receive_trigger_activate'):
            out.append((r['receive_trigger_activate'], 'started by'))
    else:
        for k in ('receive_trigger_activate', 'listen_trigger_open', 'receive_trigger_kill'):
            if r.get(k):
                out.append((r[k], k))
        if r.get('takes_damage') or r.get('max_health'):
            note = f"destroy it (max_health {r.get('max_health')})"
    return out, note


def sender_line(s):
    return {'id': s.get('id'), 'template': s.get('template'), 'property': s.get('property'), 'pos': pos(s.get('position'))}


def trace(t, seen, depth=0):
    node = {'trigger': t, 'senders': []}
    d = trig.get(t)
    if t == mission.get('peasant_success_trigger') and mission.get('peasant_max_saved'):
        node['mission'] = f"peasant_success_trigger: {mission.get('peasant_max_saved')} peasants saved (each peasant_saved_trigger {mission.get('peasant_saved_trigger')})"
    if not d:
        node['note'] = 'no sender in the data'
        return node
    if d.get('code_sender'):
        node['code'] = d['code_sender']
    if not d.get('senders') and not d.get('code_sender') and 'mission' not in node:
        node['note'] = 'NO SENDER: never sent'
    for s in d.get('senders', []):
        sl = sender_line(s)
        if s.get('id') in inst and depth < 12:
            wait, note = waits_on(s['id'])
            if note:
                sl['note'] = note
            sl['waits'] = []
            for (wt, why) in wait:
                if wt in seen:
                    sl['waits'].append({'trigger': wt, 'why': why, 'seen': True})
                    continue
                seen.add(wt)
                sub = trace(wt, seen, depth + 1)
                sub['why'] = why
                sl['waits'].append(sub)
        node['senders'].append(sl)
    return node


def show(node, ind=''):
    head = f"{ind}trigger {node['trigger']}" + (f" ({node['why']})" if node.get('why') else '')
    if node.get('seen'):
        print(head + ': (above)')
        return
    extra = [node.get('mission'), node.get('note'), node.get('code') and f"code: {node['code']}"]
    print(head + (': ' + '; '.join(x for x in extra if x) if any(extra) else ''))
    for s in node.get('senders', []):
        print(f"{ind}  <- {s['id']} {s['template']}.{s['property']} at {s['pos']}" + (f"  [{s['note']}]" if s.get('note') else ''))
        for w in s.get('waits', []):
            show(w, ind + '      ')


# where the player gets ticker bombs: the mission's loadout or a pub's award (has_tickerbomb; listen_trigger_open opens it)
tickers_from = ['mission loadout'] if mission.get('player_has_tickerbomb') else []
for i in m['instances']:
    if i['template'] == 'pub' and i['resolved'].get('has_tickerbomb'):
        tickers_from.append(f"pub {i['id']} at {pos(i['position'])}"
                            + (f" (opened by trigger {i['resolved']['listen_trigger_open']})" if i['resolved'].get('listen_trigger_open') else ''))
roots = {'win': mission.get('receive_trigger_success'), 'pre_success': mission.get('receive_trigger_pre_success')}
for k in ('', '_2', '_3', '_4'):
    roots['fail' + k] = mission.get('receive_trigger_failure' + k)
if mission.get('peasant_success_trigger') and mission.get('peasant_max_saved'):
    roots['peasants'] = mission.get('peasant_success_trigger')
roots = {k: v for k, v in roots.items() if v}
out = {'level': a.level, 'next_level': mission.get('next_level_name'), 'compass_objective_tag': mission.get('compass_objective_tag'),
       'peasants': {k: mission.get(k) for k in ('peasant_max_saved', 'peasant_max_casualties', 'peasant_saved_trigger', 'peasant_success_trigger') if mission.get(k)},
       'ticker_bombs_from': tickers_from,
       'chains': {k: trace(v, {v}) for k, v in roots.items()}}
if a.all:
    out['graph'] = {t: {'senders': [sender_line(s) for s in d.get('senders', [])],
                        'receivers': [{'id': r.get('id'), 'template': r.get('template'), 'property': r.get('property')} for r in d.get('receivers', [])]}
                    for t, d in sorted(trig.items())}
if a.json:
    json.dump(out, sys.stdout, indent=1)
    sys.exit(0)
print(f"{a.level} -> {out['next_level']}  compass tag {out['compass_objective_tag']}" + (f"  peasants {out['peasants']}" if out['peasants'] else ''))
if out['ticker_bombs_from']:
    print('ticker bombs from: ' + '; '.join(out['ticker_bombs_from']))
for k, n in out['chains'].items():
    print(f"\n[{k}]")
    show(n, '  ')
if a.all:
    print('\n[graph]')
    for t, d in out['graph'].items():
        s = '; '.join(f"{x['id']}:{x['template']}.{x['property']}" for x in d['senders']) or '-'
        r = '; '.join(f"{x['id']}:{x['template']}.{x['property']}" for x in d['receivers']) or '-'
        print(f"{t:>8} | {s} ==> {r}")
