A LightDM greeter for Prologin SADM environment.

Features a webkit-based login experience for easy theming.

Licensed under GNU General Public License v2.0, see [COPYING](./COPYING).

## Build status

![build](https://github.com/prologin/lightdm-prologin-greeter/workflows/build/badge.svg)

## Usage

Once the [package](./archlinux) is installed, set the correct greeter session
in `/etc/lightdm/lightdm.conf`:

```ini
[Seat:*]
greeter-session=lightdm-prologin-greeter
```

## Configuration

The greeter reads `/etc/lightdm/lightdm-prologin-greeter.conf` at startup. This
is an INI file that can contain the following:

```ini
[greeter]
; Page to load. If it fails, the greeter fallbacks to a built-in theme.
url = "http://greeter/"
; A valid QColor string to use as solid background color while loading the URL.
; https://doc.qt.io/qt-5/qcolor.html#setNamedColor
background_color = "#123abc"
; Seconds to wait for the page to load before falling-back.
fallback_delay = 3
```

## Fallback theme

The built-in fallback theme is bare-bones but contains all that necessary bits
to interact with the login manager, which isn't a trivial amount of code. To
make it easy to customize the theme without having to create JS and CSS from 
scratch, the fallback theme references `http://greeter/theme.css` and
`http://greeter/theme.js` after the default resources. By making these files 
available on the network, you can easily override the default look and behavior.
