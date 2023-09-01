# ANAVI Thermometer display support

ANAVI Thermometer has support for adding a 128x64 OLED display via a
SSD1306 driver.  It uses the U8g2 Arduino library to write to the
display.

By default, it will display the temperature and humidity.  If more
supported sensors are connected, some of those values will also be
displayed.  Using MQTT, you can set a format string that overrides the
default, so that you can decide exactly what to display.

These OLED displays are susceptible to burn-in, so avoid displaying
the same thing for a long time.

## MQTT topics

To print a custom message to the display, prepare a format string (see
the next section), and send it to the `cmnd/$DEV/graph/1` MQTT topic,
where `$DEV` is the device ID of your ANAVI Thermometer.  If the
string is too long, you can split it in up to 10 parts and send the
second part to `cmnd/$DEV/graph/2`, et c.  (ANAVI Thermometer can only
handle MQTT messages of a certain size.  If you send a message that is
too long, it will be silently truncated.)

If the concatenation of `cmnd/$DEV/graph/N` (for `N` in 0..9) is
empty, ANAVI Thermometer will instead use a builtin default format
string.  Actually, it has a few builtin format strings, and the
`cmnd/$DEV/display` MQTT topic determins which one to use.  The
default is 0.  This table describes the available strings:

| String | Description |
| - | - |
| 0 | Legacy format |
| 1 | Small humidity, big temperature |
| 2 | Display "everything" |
| 3 | Screen test: blank screen |
| 4 | Screen test: turn every pixel on |
| 5 | Screen test: turn on every other pixel |

## Formatting overview

The display language is based on one-character commands.  Some of them
take one or two numeric prefix arguments.  For example, `C` clears the
display, `1f` selects font number 1, `10V` displays the value of
sensor 10 (which is the temperature as measured by the DHT22 sensor),
and `1,10V` displays the same temperature but without the trailing `F`
or `°C`.

Here is a brief list of available commands.  `M` and `N` are numeric
arguments to the command.  They default to 0 unless otherwise noted.

| Code | Description |
| ---: | :--- |
|   `C` | Clear the display and move to x=0, y=0. |
| *N*`(`...`)` | Repeat ... *N* times. |
| *N*`f`  | Select font *N*. |
| *N*`H`... | Display a hollerith-encoded string of ASCII characters. |
| `[`...`]` | Display a UTF-8-encoded string. |
| *N*`x` | Set the x coordinate. |
| *N*`y` | Set the y coordinate. |
| *N*`X` | Set the x delta.  Used by the '.' and ' ' commands. |
| *N*`Y` | Set the y delta.  Used by the '.' and ' ' commands. |
| `<` | Set drawing direction to "left".  Shorthand for `-1X0Y`. |
| `>` | Set drawing direction to "right".  Shorthand for `1X0Y`. |
| `^` | Set drawing direction to "up".  Shorthand for `0X-1Y`. |
| `v` | Set drawing direction to "down".  Shorthand for `0X1Y`. |
| *N*`.` | Draw N pixels in the current drawing direction. |
| *N*` ` | Move N pixels forward in the current drawing direction. |
| *N*`b` | Move N pixels backwards. |
| *N*`h` | Move to the beginning of line *N* (0, 1, 2 or 3) |
| *N*`c` | Set the contrast.  See `U8g2.setContrast`.
| *N*`P` | Enable powersave mode if *N* is positive, disable if 0. |
| *N*`i` | Invert the display if *N* is positive. |
| `B` | Enable burn-in protection by sometimes inverting the screen. |
| *N*`V` | Display the value of sensor *N*. |
| *N*`E` | Interpret a user-defined value as a format string. |
| *M*,*N*`{`...`}` | Alternate screen.  See below. |
| *N*`?`...`;` | Conditional rendering (if-then) |
| *N*`?`...`:`...`;` | Conditional rendering (if-then-else) |

### Coordinates and line-drawing

The (0,0) coordinate is the top left corner, and (63,127) is the
bottom right corner.

You can use the `x` and `y` commands to set the x and y coordinate.
Since 0 is the default value the string `xy` will move the active
position to the top left corner.

The `.` command turns on the pixel at the active coordinate, and moves
one step forward.  This means that you can draw a short line at the
top left corner using this format string: `xy.....`.

Since it is common to draw several consecutive dots, you can use a
prefix argument to specify how many dots you want.  You can draw a
line that stretches along the entire line of the display like this:
`xy128.`.

By default, `.` moves one pixel to the right.  But you can use the `X`
and `Y` commands to set any direction you want.  This will draw a
dotted diagonal line across the entire display: `xy4X2Y32.`

Since moving to the top left corner is so common, the `h` command
(without a prefix argument) can be used to do just that.  So `xy` and
`h` are equivalent.

Some directions are more commonly used than others.  They have special
shortcut commands:

| Command | Resulting direction |
| - | - |
| `<` | Left |
| `>` | Right |
| `v` | Down |
| `^` | Up |

The ` ` command (a single space) moves the active coordinate, just
like `.`, but does not turn on any pixel.  (It does not turn off any
pixel either.  It just moves the cursor.)  The `b` command is similar,
but moves in the opposite direction.  Both accept a prefix argument as
a repeat count.

### Writing text

To print a fixed string on the display, you can use the `H` command.
Old-time FORTRAN-66 programmers might recognize this as a Hollerith
constant.  To print the string "hello", write `5Hhello`.  That is, the
`H` command uses the prefix argument as a length (in bytes) and uses
the following *N* bytes as the value to print.  Only ASCII characters
in the range 32-126 should be used.

You can also use the `[`...`]` construct to print text.  The bytes
between `[` and `]` are interpreted as UTF-8 (but only code-points
32-255 are supported).  It is impossible to print a literal `]`
character using this method; use "1H]" for that.  Nevertheless, `[` is
recommended instead of `H` for all other uses.

Text is written at the active cursor position, and the cursor position
is updated.  Text is always written towards the right, so the
direction set by the `X` and `Y` commands do not affect printed
strings.

When printing a letter, the lower left corner of the letter is printed
at the active position.  This means that if you start at (0,0) and
write text, it will be written above (outside of) the display.  This
is typically now what you want.

The `h` command takes a line number as a prefix argument.

| Command | Position | Description |
| - | - | - |
| `0h` or `h` | (0, 0) | Top left corner |
| `1h` | (0, 14) | Top line if font 0 is used. |
| `2h` | (0, 39) | Middle line if font 0 is used. |
| `3h` | (0, 60) | Bottom line. |

The `h` command can be convenient. `0x39y` has the same effect as
`2h`.

The font can be selected with the `f` command.  The following fonts
are available:

| Command | Font |
| - | - |
| `0f` | `ncenR14` (a tiny font) |
| `1f` | `ncenB24` (a small font) |
| `2f` | `logisoso46` (a large font) |

### Sensor values

Sensor values can be written to the display using the `V` command.  It
takes the sensor ID as a prefix argument.

By default, temperature values include a trailing unit ("F" or "°C").
This can be disabled by specifying an extra prefix argument: `10V`
might print "22.8°C"; `1,10V` would then print "22.8".

The following sensors are defined:

| Sensor number | Description |
| - | - |
| 0 | User-defined value 0 |
| 1 | User-defined value 1 |
| 2 | User-defined value 2 |
| 3 | User-defined value 3 |
| 4 | User-defined value 4 |
| 5 | User-defined value 5 |
| 6 | User-defined value 6 |
| 7 | User-defined value 7 |
| 8 | User-defined value 8 |
| 9 | User-defined value 9 |
| 10 | The DHT22 temperature |
| 11 | The DHT22 humidity |
| 12 | The BMP180 barometric pressure |
| 13 | The BH1750 ambient light level |
| 14 | The DS18B20 temperature (not in `MULTI_DS18B20_SUPPORT` mode) |
| 15 | The current time (in UTC) |
| 16 | The current RSSI value (in dBm) |

If a particular sensor isn't available, the `V` command will print an
empty string.

### User-defined values

The ANAVI Thermometer supports 10 user-defined values, numbered 0..9.
These are defined by sending MQTT messages to the topic
`$WORKGROUP/$DEV/value/$N`, where `$N` is 0..9.  The message must be a
JSON-encoded dictionary that defines how the value should be set.
These formats are supported:

| Format | Effect |
| - | - |
| `{ "v": "`*string*`" }` | Set the user-defined value to *string* |
| `{ "ds18b20": "`id`" }` | Set to the temperature measured by sensor *id* |
| `{ "ds18b20": "`id`", "include-unit": true }` | Also include "°C" or "F" |

The DS18B20 forms only work if `MULTI_DS18B20_SUPPORT` was enabled
when compiling the sketch.  The ID can be seen by watching the MQTT
messages; if a sensor publishes to
`workgroup/a78b93e45/ds18b20/28d18f81e3ca3c72/temperature`, the value
to use is `28d18f81e3ca3c72`.

It is possible to store a format string in a user-defined value, and
use the `E` command to interpret it.  This could potentially be useful
as subroutines when doing repetitive line drawings.

### Conditional rendering

You can use the `?` construct to render a part only if a sensor is
available.  The full form looks like this:

*N*`?`...`:`...`;`

It renders the part between `?` and `:` if sensor *N* is available,
the part between `:` and `;` if it isn't.  The `:` and "else" part is
optional.

You can also use a second prefix argument: *M*`,`*N*`?`...`;`.  This
renders ... if at least one of the sensors *M* and *N* are available.

### Alternate screens

The display is fairly small.  What if you want to display more values
than fit?  One way do do that is to use the `{` command, that allows
you to alternate between several information screens.  *N*`{...}`
prints `...`, but only when screen *N* is active.  *N* should be a
small number starting with 0.  To alternately display user-defined
values 0, 1 and 2, you can write:

```
0{0V}1{1V}2{2V}
```

Actually, you can have up to 10 sets of screens.  Each set is numbered
0-9, and each set works independently of the other sets.  To display
user-defined value 0, 1 and 2 on line 1, and 3 and 4 on line 2, write:

```
1h0,0{0V}0,1{1V}0,2{2V}
2h1,0{3V}1,1{4V}
```

The special value -1 can be used to allocate a new number
automatically.  This is especially useful when combined with
conditional rendering:

```
1h12?-1{5HBaro 12V};13?-1{6HLight 13V};
```

### Example

The following commands sets up a display format that displays "ANAVI
Thermometer" and a small thermometer icon on the blue parts of the
display, and alternates between showing the air temperature, the
humidity, and the temperature measured by a connected DS18B20 sensor.

It requires the following environment variables to be set:

| Variable | Description |
| - | - |
| U | MQTT username |
| P | MQTT password |
| SERVER | MQTT server |
| DEV | ANAVI Thermometer device ID |

```
# Display the string "ANAVI Thermometer".
mosquitto_pub \
    -u "$U" -P "$P" \
    -h "$SERVER" \
    -r \
    -t cmnd/$DEV/graph/1 \
    -m 'C1f2h2 v    [ANAVI]f3h2 [Thermometer]'

# Display the temperature, humidity, and (if available) DS18B20
# temperature.  Note that this displays both user value 0 (which is
# set up below to contain the temperature of a selected DS18B20
# sensor) and value 14.  Value 0 is only available in builds that use
# MULTI_DS18B20_SUPPORT, and value 14 is only available in builds
# without MULTI_DS18B20_SUPPORT.
#
# Also, enable the burn-in protection.
mosquitto_pub \
    -u "$U" -P "$P" \
    -h "$SERVER" \
    -r \
    -t cmnd/$DEV/graph/2 \
    -m '5x16y{[Air ]10V}1{[Humid ]11V[%]}?-1{[DS18 ]V};14?-1{[DS18 ]14V};B'

# Draw a little thermometer to the right.
mosquitto_pub \
    -u "$U" -P "$P" \
    -h "$SERVER" \
    -r \
    -t cmnd/$DEV/graph/3 \
    -m '120x40y4.^ <6.^ >7.^.<7.^.>7.^.< 6.^ >  3.^.4(<3.^.>3.^.)4(<.  ^.>.  ^.)< ..'

# Configure which DS18B20 should be displayed.  You will need to
# change the "28d18f81e3ca3c72" to the ID of your sensor.  This is
# only needed if the firmware has been compiled with
# MULTI_DS18B20_SUPPORT enabled.
mosquitto_pub \
    -u "$U" -P "$P" \
    -h "$SERVER" \
    -r \
    -t cmnd/$DEV/value/0 \
    -m '{ "ds18b20": "28d18f81e3ca3c72", "include-unit": true }'
```

The end result looks like this:

![Photo of a display showing the output of the above display
format.](demo.jpg).
