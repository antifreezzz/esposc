// Проверка шаблона 1:1 из enclosure3d.html. Запуск: открыть страницу в Chrome и выполнить эту функцию
// (chrome-devtools evaluate_script с sourcePath). Возвращает список провалов, пустой - всё хорошо.
() => {
  const bad = [];
  const ok = (c, m) => { if (!c) bad.push(m); };
  const near = (a, b, e = .05) => Math.abs(a - b) <= e;
  if (typeof templateSheets !== 'function') return ['templateSheets нет'];

  const sc = buildScene('plate', 'dac', 'combo');
  const sh = templateSheets(sc);
  const by = n => sh.find(s => s.name === n);
  for (const n of ['Дно', 'Крышка', 'Задняя стенка', 'Передняя стенка', 'Левая стенка', 'Правая стенка'])
    ok(by(n), `нет листа ${n}`);
  ok(!sh.some(s => ['Угол', 'Стойки крышки', 'Ножки'].includes(s.name)), 'лишние детали в шаблоне');

  const lid = by('Крышка');
  if (lid) {
    ok(near(lid.w, 165) && near(lid.h, 119), `крышка ${lid.w} x ${lid.h}, ждали 165 x 119`);
    ok(near(lid.thick, 2.5), `толщина крышки ${lid.thick}`);
    ok(lid.through.length >= 1 + 4 + 4, 'у крышки мало сквозных вырезов (окно, 4 винта, 4 энкодера)');
  }
  const rear = by('Задняя стенка');
  if (rear) {
    const win = rear.through.filter(h => near(h.bw, 9.4) && near(h.bh, 3.8));
    const pock = rear.pockets.filter(h => near(h.bw, 12.8) && near(h.bh, 7.2));
    ok(win.length >= 1, 'на задней стенке нет сквозного окна USB-C 9.4 x 3.8');
    ok(pock.length >= 1, 'на задней стенке нет кармана 12.8 x 7.2');
    ok(!rear.through.some(h => near(h.bw, 12.8)), 'карман USB-C попал в сквозные');
    // вид снаружи: сзади правый край корпуса (x = 143, разъём 5V) виден слева
    const pwr = win.find(h => near(h.cx, sc.X - 143, .1));
    ok(pwr, `5V не на ${sc.X - 143} от левого края задней стенки снаружи: ${win.map(h => h.cx.toFixed(1))}`);
  }
  const floor = by('Дно');
  if (floor) ok(floor.marks.length >= 4, 'на дне не отмечены стойки');

  const html = templateHTML(sc);
  ok(/width="190mm"/.test(html) && /height="277mm"/.test(html), 'листы не в миллиметрах A4');
  ok(/100 мм/.test(html), 'нет контрольной линейки 100 мм');
  ok((html.match(/<svg/g) || []).length >= 2, 'всё на одном листе: ждали несколько страниц');
  for (const f of ['wedge', 'pult', 'cube', 'box']) {
    try { const t = templateSheets(buildScene(f, 'dac', 'combo')); ok(t.length >= 4, `${f}: ${t.length} листов`); }
    catch (e) { bad.push(`${f}: ${e.message}`); }
  }
  return bad;
}
