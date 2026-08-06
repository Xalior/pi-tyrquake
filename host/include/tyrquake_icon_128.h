/*
 * tyrquake_icon_128.h — the window icon, absent.
 *
 * TyrQuake's SDL layer sets a window icon from a 128x128 RGBA array that
 * upstream's own build generates from icons/tyrquake-1024x1024.png with
 * ImageMagick. When ImageMagick is not installed, upstream's build writes
 * this file with `#define DISABLE_ICON` in it instead, and the icon code
 * compiles away. That is the same file, written down rather than generated.
 *
 * A window icon is a desktop idea. There is no window manager on this board,
 * no title bar and no task switcher, so there is nowhere for an icon to
 * appear and nothing is lost by not having one.
 */
#define DISABLE_ICON
