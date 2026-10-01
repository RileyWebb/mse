#ifndef FRONTEND_ICONS_H
#define FRONTEND_ICONS_H

// Nerd Font glyphs from data/fonts/JetBrainsMono.ttf, as UTF-8.
//
// Prefer Font Awesome (U+F000-U+F3FF). Those are loaded by both the icon face
// and the body face, so they draw wherever they are put -- in a sidebar row,
// which uses the icon face, or inline in a button label, which uses the body
// one. The other Nerd Font ranges are only loaded by the icon face, and
// Material Design's past U+F0379 by neither: an icon from there shows as a
// missing glyph, as the controller and terminal icons below used to.

#define MSE_ICON_HOME "\xEF\x91\xAD"
#define MSE_ICON_SYSTEMS "\xE2\x8C\x82"
#define MSE_ICON_UPDATE_ALL "\xE2\x9F\xB3"
#define MSE_ICON_FETCH_CORES "\xEF\x80\x99"
#define MSE_ICON_START_CORE "\xE2\x96\xB6"
#define MSE_ICON_UPDATE "\xEF\x83\xAD"
#define MSE_ICON_REMOVE "\xEF\x80\x8D"
#define MSE_ICON_OPTIONS "\xE2\x9A\x99"
#define MSE_ICON_INFO "\xEF\x81\x9A"
#define MSE_ICON_INSTALLED "\xE2\x9C\x93"
#define MSE_ICON_NEEDS_UPDATE "\xEF\x83\xAD"
#define MSE_ICON_DOWNLOAD "\xEF\x80\x99"
#define MSE_ICON_SETTINGS "\xEF\x80\x93"
#define MSE_ICON_CONTROLLER "\xEF\x84\x9B"
#define MSE_ICON_KEYBOARD "\xEF\x84\x9C"
#define MSE_ICON_APPEARANCE "\xEF\x87\xBC"
#define MSE_ICON_WRENCH "\xEF\x82\xAD"
#define MSE_ICON_POWER "\xEF\x80\x91"
#define MSE_ICON_VIDEO "\xF3\xB0\x8D\xB9"
#define MSE_ICON_VOLUME_LOW "\xEF\x80\xA7"
#define MSE_ICON_VOLUME_HIGH "\xEF\x80\xA8"
#define MSE_ICON_VOLUME_OFF "\xEF\x80\xA6"
#define MSE_ICON_VOLUME_MUTE "\xEE\xBB\xA8"
#define MSE_ICON_TERMINAL "\xEF\x84\xA0"
#define MSE_ICON_LIBRARY "\xEE\xAE\x9C"
#define MSE_ICON_BACKENDS "\xEF\x92\xBC"
#define MSE_ICON_BIOS "\xEF\x8B\x9B"
#define MSE_ICON_MEMVIEW "\xEF\x87\x80"
#define MSE_ICON_LOGS "\xEF\x83\xB6"
#define MSE_ICON_ADD "\xEF\x81\xA7"
#define MSE_ICON_SEARCH "\xEF\x80\x82"
#define MSE_ICON_CREDITS "\xEF\x83\x80"
#define MSE_ICON_LICENCE "\xEF\x89\x8E"
#define MSE_ICON_PERFORMANCE "\xEF\x83\xA4"
#define MSE_ICON_ADVANCED "\xEF\x87\x9E"

#endif // FRONTEND_ICONS_H