# Скрипты блоков (Lua 5.4)

Скрипт задаёт, что блок **делает**. Файл — `assets/scripts/<имя>.lua`. Блок подключает его строкой в своём файле `assets/blocks/<блок>.json`:

```json
"script": "farmland"
```

Можно указать подпапку: `"script": "farm/wheat"` → `assets/scripts/farm/wheat.lua`.

После правки скрипта не нужно перезапускать игру: введи в чате **`/reload`**. Ошибки скриптов и вывод `print()` появляются в чате, полный текст ошибки пишется в лог.

## Как устроен скрипт

Скрипт возвращает таблицу событий. Описывать нужно только те события, которые нужны.

```lua
return {
    -- Можно ли игроку поставить блок сюда (клетка ещё пустая). false — нельзя.
    can_place = function(pos) return true end,
    -- Может ли блок остаться на месте. Вызывается, когда меняется сосед; false — блок ломается с дропом.
    can_stay = function(pos) return true end,

    on_placed = function(pos) end,                 -- блок только что появился (поставил игрок или скрипт)
    on_broken = function(pos) end,                 -- блок только что сломан (клетка уже пустая)
    on_use = function(pos, use) return false end,  -- ПКМ по блоку; true — клик использован
    on_random_tick = function(pos) end,            -- случайный тик: у одного блока в среднем раз в ~68 с
    on_scheduled_tick = function(pos) end,         -- наступил тик, заказанный через world.schedule_tick
    on_neighbor_changed = function(pos, from) end, -- соседний блок (from) изменился
}
```

## Позиции

`pos` — таблица `{x, y, z}` с методами:

| Метод | Что даёт |
|---|---|
| `pos(x, y, z)` | новая позиция |
| `p:above(n)`, `p:below(n)` | выше и ниже на n (по умолчанию 1) |
| `p:north(n)`, `p:south(n)`, `p:east(n)`, `p:west(n)` | по сторонам света (north = −Z, east = +X) |
| `p:offset(dx, dy, dz)` | сдвиг |
| `p:sides()` | четыре соседа по бокам |
| `a == b`, `tostring(p)` | сравнение и текст |

## `world` — API движка

Блоки и предметы задаются **именами**: `"water"`, `"oak_log"`, `"stick"`.

**Запросы**

| Функция | Результат |
|---|---|
| `world.get_block(pos)` | имя блока (`"air"`, если пусто) |
| `world.get_property(pos, name)` | значение свойства состояния или `nil`, если у блока такого нет |
| `world.get_light(pos)` | 0..15: свет сейчас (небо с учётом времени суток или факелы) |
| `world.get_sky_light(pos)`, `world.get_block_light(pos)` | 0..15: каналы по отдельности, небо как в полдень |
| `world.is_block_near(pos, block, radius, height)` | есть ли `block` в радиусе `radius` по бокам и `height` вверх и вниз (по умолчанию `height = radius`) |
| `world.random()` | случайное число 0..1 |
| `world.random(a, b)` | случайное целое a..b |
| `world.tick()` | номер игрового тика (20 в секунду) |

**Действия**

| Функция | Что делает |
|---|---|
| `world.set_block(pos, block)` | ставит блок (соседи получают `on_neighbor_changed`) |
| `world.set_property(pos, name, value)` | меняет свойство состояния (число или `true`/`false`) |
| `world.break_block(pos, drops)` | ломает блок с частицами и звуком; `drops = false` — без дропа |
| `world.drop_item(pos, name, count)` | выбрасывает предмет или блок |
| `world.schedule_tick(pos, delay)` | `on_scheduled_tick` через `delay` тиков, если блок ещё тот же |
| `world.place_structure(pos, name)` | выращивает структуру из `assets/structures`; `false`, если мешает обязательный блок |

## `use` — что пришло в `on_use`

| Поле | Значение |
|---|---|
| `use.face` | по какой грани кликнули: `"top"`, `"bottom"`, `"north"`, … |
| `use.creative` | игрок в творческом режиме |
| `use.held` | `{ name = "wheat_seeds", count = 5 }` или `nil`, если рука пустая |
| `use:take(n)` | забрать n (по умолчанию 1) из руки; в творческом ничего не забирает |

## Свойства состояния

Блок хранит свои значения, например стадию роста. Их объявляют в файле блока:

```json
"properties": [
    { "name": "age", "max": 7 },
    { "name": "moist", "max": 1, "default": 0 }
]
```

`max: 1` — это «да/нет». На все свойства одного блока приходится до 32 бит.

## Ограничения

- Нет доступа к файлам и ОС: `io`, `os`, `require`, `load` недоступны.
- Вызов, который выполняется слишком долго (бесконечный цикл), останавливается с ошибкой.
- Ошибка останавливает только этот вызов. Об одной и той же ошибке сообщается один раз до следующего `/reload`.
- Заказанные тики (`schedule_tick`) пока не сохраняются вместе с миром.

## Пример

`assets/blocks/sponge.json` → `"script": "test"`, файл `assets/scripts/test.lua`:

```lua
return {
    on_use = function(pos, use)
        print("клик по губке в " .. tostring(pos) .. " гранью " .. use.face)
        world.set_block(pos, "gold_block")
        return true
    end,
}
```
