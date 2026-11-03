// Проверка экспорта 3MF для печати из enclosure3d.html. Запуск как у enclosure3d_check.js: открыть страницу в Chrome
// и выполнить эту функцию (chrome-devtools evaluate_script с sourcePath). Возвращает список провалов, пустой - всё хорошо.
() => {
  const bad = [];
  const ok = (c, m) => { if (!c) bad.push(m); };
  const near = (a, b, e = .05) => Math.abs(a - b) <= e;
  if (typeof printModel !== 'function' || typeof make3mf !== 'function' || typeof crc32 !== 'function')
    return ['printModel, make3mf или crc32 нет'];

  const bb = o => {
    const mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9];
    for (const v of o.verts) for (let k = 0; k < 3; k++) { mn[k] = Math.min(mn[k], v[k]); mx[k] = Math.max(mx[k], v[k]); }
    return { mn, mx, d: [0, 1, 2].map(k => mx[k] - mn[k]) };
  };
  // замкнутость и согласованный обход: каждое ребро a->b встречается столько же раз, сколько b->a
  const unbalanced = o => {
    const m = new Map(); let n = 0;
    for (const [a, b, c] of o.tris) for (const [p, q] of [[a, b], [b, c], [c, a]]) m.set(p + ',' + q, (m.get(p + ',' + q) || 0) + 1);
    for (const [k, c] of m) { const [p, q] = k.split(','); if ((m.get(q + ',' + p) || 0) !== c) n++; }
    return n;
  };
  // объём со знаком: > 0, если нормали наружу
  const vol = o => o.tris.reduce((s, [a, b, c]) => {
    const [p, q, r] = [o.verts[a], o.verts[b], o.verts[c]];
    return s + (p[0] * (q[1] * r[2] - q[2] * r[1]) - p[1] * (q[0] * r[2] - q[2] * r[0]) + p[2] * (q[0] * r[1] - q[1] * r[0])) / 6;
  }, 0);
  // площадь граней на столе (z = 0), смотрящих вниз, и верхних (z = max), смотрящих вверх
  const flatArea = (o, top) => {
    const z = top ? bb(o).mx[2] : 0;
    return o.tris.reduce((s, [a, b, c]) => {
      const [p, q, r] = [o.verts[a], o.verts[b], o.verts[c]];
      if (![p, q, r].every(v => Math.abs(v[2] - z) < .01)) return s;
      const cz = (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
      return (top ? cz > 0 : cz < 0) ? s + Math.abs(cz) / 2 : s;
    }, 0);
  };
  // лицом вниз: наружная плоскость сплошная, внутри бортики, поэтому на столе площади больше, чем сверху
  const faceDown = o => flatArea(o, false) > flatArea(o, true);
  const common = (pm, f) => {
    ok(pm.objects.length >= 2, `${f}: ${pm.objects.length} объектов`);
    for (const o of pm.objects) {
      const b = bb(o), u = unbalanced(o), v = vol(o);
      ok(near(b.mn[2], 0, .01), `${f}/${o.name}: низ на ${b.mn[2].toFixed(2)}, не на столе`);
      ok(u === 0, `${f}/${o.name}: ${u} несогласованных рёбер`);
      ok(v > 0, `${f}/${o.name}: объём ${v.toFixed(0)}, нормали внутрь`);
    }
    // детали не налезают друг на друга на столе
    const bs = pm.objects.map(bb);
    for (let i = 0; i < bs.length; i++) for (let j = i + 1; j < bs.length; j++) {
      const a = bs[i], c = bs[j];
      ok(a.mx[0] <= c.mn[0] || c.mx[0] <= a.mn[0] || a.mx[1] <= c.mn[1] || c.mx[1] <= a.mn[1],
        `${f}: ${pm.objects[i].name} и ${pm.objects[j].name} налезают`);
    }
  };

  const sc = buildScene('plate', 'dac', 'combo');
  const pm = printModel(sc);
  common(pm, 'plate');
  const by = n => pm.objects.find(o => o.name === n);
  ok(pm.objects.map(o => o.name).sort().join() === ['Вставка: матрица 4x4 + тач', 'Корпус', 'Крышка'].sort().join(),
    `plate: объекты ${pm.objects.map(o => o.name)}`);
  const tub = by('Корпус');
  if (tub) {
    const b = bb(tub);
    ok(near(b.d[0], 165) && near(b.d[1], 119), `корпус ${b.d[0].toFixed(2)} x ${b.d[1].toFixed(2)}, ждали 165 x 119`);
    // правый мир без зеркала: задняя стенка сзади (большой y), разъём 5V справа (x = 143 + T от левого края)
    const atRear = x => tub.verts.some(v => v[1] > b.mx[1] - .01 && Math.abs(v[0] - b.mn[0] - x) <= 6.5 && v[2] > .5);
    ok(atRear(143 + 2.5), 'на задней стенке нет кармана 5V справа: модель зеркальна');
    ok(!atRear(160 - 143 + 2.5), 'карман 5V слева: модель зеркальна');
  }
  const lid = by('Крышка');
  if (lid) {
    const b = bb(lid);
    ok(near(b.d[0], 165) && near(b.d[1], 119), `крышка ${b.d[0].toFixed(2)} x ${b.d[1].toFixed(2)}`);
    ok(b.d[2] < 10, `крышка высотой ${b.d[2].toFixed(1)}: не лежит плашмя`);
    ok(faceDown(lid), `крышка не лицом вниз: на столе ${flatArea(lid).toFixed(0)}, сверху ${flatArea(lid, true).toFixed(0)}`);
  }

  const wl = printModel(buildScene('wedge', 'dac', 'blank'));
  common(wl, 'wedge');
  const wlid = wl.objects.find(o => o.name === 'Наклонная крышка');
  ok(wlid, 'wedge: нет наклонной крышки');
  if (wlid) {
    ok(bb(wlid).d[2] < 10, `наклонная крышка высотой ${bb(wlid).d[2].toFixed(1)}: не уложена плашмя`);
    ok(faceDown(wlid), 'наклонная крышка не лицом вниз');
  }
  for (const f of ['pult', 'cube', 'box']) {
    try { common(printModel(buildScene(f, 'dac', 'blank')), f); } catch (e) { bad.push(`${f}: ${e.message}`); }
  }
  const cube = printModel(buildScene('cube', 'dac', 'blank'));
  ok(cube.objects.some(o => o.name === 'Рамка-переходник'), 'cube: рамка не отдельным объектом');

  ok(crc32(new TextEncoder().encode('123456789')) === 0xCBF43926, 'crc32 неверный');
  const z = make3mf(sc), txt = new TextDecoder().decode(z);
  ok(z[0] === 0x50 && z[1] === 0x4B, '3mf не zip');
  for (const s of ['[Content_Types].xml', '_rels/.rels', '3D/3dmodel.model', 'unit="millimeter"', 'name="Корпус"'])
    ok(txt.includes(s), `в 3mf нет ${s}`);
  return bad;
}
