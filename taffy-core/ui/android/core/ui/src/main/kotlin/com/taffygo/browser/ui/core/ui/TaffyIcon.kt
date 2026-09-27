package com.taffygo.browser.ui.core.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.PathParser
import androidx.compose.ui.unit.dp

/**
 * The icon set every Taffy-owned surface draws from.
 *
 * A subset of seventy-eight Phosphor glyphs (MIT licence, provenance in
 * `core/ui/vendor/phosphor.txt`), vendored as path data rather than pulled in
 * as a dependency so the UI layer carries no artifact the fork would have to
 * vendor again. Weights follow `handoff/DESIGN.md` section 4: regular for
 * controls, fill for the status glyphs, bold only for the check.
 *
 * Every glyph is drawn in the current tint, so a glyph never names a colour;
 * the component that places it does.
 */
object TaffyIcon {
    /** A closed padlock: the page's connection. */
    val LockSimple: ImageVector by lazy { phosphor(PATH_LOCK_SIMPLE, "LockSimple") }

    /** A shield with a check: the blocker's blocked count. */
    val ShieldCheck: ImageVector by lazy { phosphor(PATH_SHIELD_CHECK, "ShieldCheck") }

    /** A clockwise arrow: reload the page. */
    val ArrowClockwise: ImageVector by lazy { phosphor(PATH_ARROW_CLOCKWISE, "ArrowClockwise") }

    /** A waveform: Taffy listening in the idle orb. */
    val Waveform: ImageVector by lazy { phosphor(PATH_WAVEFORM, "Waveform") }

    /** A speaker with sound waves: showcase sound is on. */
    val SpeakerHigh: ImageVector by lazy { phosphor(PATH_SPEAKER_HIGH, "SpeakerHigh") }

    /** A crossed speaker: showcase sound is off. */
    val SpeakerSlash: ImageVector by lazy { phosphor(PATH_SPEAKER_SLASH, "SpeakerSlash") }

    /** Two bars: a running thing held. */
    val Pause: ImageVector by lazy { phosphor(PATH_PAUSE, "Pause") }

    /**
     * The way out of a hold, and never the hold itself.
     *
     * A paused pill drew [Pause] — the glyph of the control that put it there,
     * on the one surface whose glyph is read as what the control does next. It
     * says the opposite of what is true, so a held task is drawn with the thing
     * that lifts it.
     */
    val Play: ImageVector by lazy { phosphor(PATH_PLAY, "Play") }

    /** A raised palm: Taffy needs a person. */
    val Hand: ImageVector by lazy { phosphor(PATH_HAND, "Hand") }

    /** A sparkle: an AI-touched value. */
    val Sparkle: ImageVector by lazy { phosphor(PATH_SPARKLE, "Sparkle") }

    /** A check mark: confirm; bold because the check has no fill weight. */
    val Check: ImageVector by lazy { phosphor(PATH_CHECK, "Check") }

    /** A checked circle: done. */
    val CheckCircle: ImageVector by lazy { phosphor(PATH_CHECK_CIRCLE, "CheckCircle") }

    /** A half-filled circle: partly done. */
    val CircleHalf: ImageVector by lazy { phosphor(PATH_CIRCLE_HALF, "CircleHalf") }

    /** A stopped circle: stopped by the user. */
    val StopCircle: ImageVector by lazy { phosphor(PATH_STOP_CIRCLE, "StopCircle") }

    /** A warning triangle: failed, or sources disagree. */
    val Warning: ImageVector by lazy { phosphor(PATH_WARNING, "Warning") }

    /** A prohibitory sign: blocked or denied. */
    val Prohibit: ImageVector by lazy { phosphor(PATH_PROHIBIT, "Prohibit") }

    /** Two windows: tabs. */
    val Browsers: ImageVector by lazy { phosphor(PATH_BROWSERS, "Browsers") }

    /** A share graph: send this somewhere. */
    val ShareNetwork: ImageVector by lazy { phosphor(PATH_SHARE_NETWORK, "ShareNetwork") }

    /** A left arrow: back. */
    val ArrowLeft: ImageVector by lazy { phosphor(PATH_ARROW_LEFT, "ArrowLeft") }

    /** A right arrow: forward. */
    val ArrowRight: ImageVector by lazy { phosphor(PATH_ARROW_RIGHT, "ArrowRight") }

    /** An up arrow: send what was written. */
    val ArrowUp: ImageVector by lazy { phosphor(PATH_ARROW_UP, "ArrowUp") }

    /** A plus: add. */
    val Plus: ImageVector by lazy { phosphor(PATH_PLUS, "Plus") }

    /** A minus: make a value smaller. */
    val Minus: ImageVector by lazy { phosphor(PATH_MINUS, "Minus") }

    /** A cross: close or dismiss. */
    val X: ImageVector by lazy { phosphor(PATH_X, "X") }

    /** A struck-through eye: nothing is watched, and nothing is sent. */
    val EyeSlash: ImageVector by lazy { phosphor(PATH_EYE_SLASH, "EyeSlash") }

    /** A pointing hand: the user is the one browsing. */
    val HandPointing: ImageVector by lazy { phosphor(PATH_HAND_POINTING, "HandPointing") }

    /** Three figures: the user and Taffy, one step each. */
    val UsersThree: ImageVector by lazy { phosphor(PATH_USERS_THREE, "UsersThree") }

    /** A pencil: rename this. */
    val PencilSimple: ImageVector by lazy { phosphor(PATH_PENCIL_SIMPLE, "PencilSimple") }

    /** A house: this stays on the device. */
    val House: ImageVector by lazy { phosphor(PATH_HOUSE, "House") }

    /** A clock turned back: history. */
    val ClockCounterClockwise: ImageVector by lazy {
        phosphor(PATH_CLOCK_COUNTER_CLOCKWISE, "ClockCounterClockwise")
    }

    /** A bookmark: the library and what it remembers. */
    val BookmarkSimple: ImageVector by lazy { phosphor(PATH_BOOKMARK_SIMPLE, "BookmarkSimple") }

    /** Lines of text: the words on a page. */
    val Article: ImageVector by lazy { phosphor(PATH_ARTICLE, "Article") }

    /** A speech bubble: what the user typed. */
    val ChatCircle: ImageVector by lazy { phosphor(PATH_CHAT_CIRCLE, "ChatCircle") }

    /** Masked characters: a credential field. */
    val Password: ImageVector by lazy { phosphor(PATH_PASSWORD, "Password") }

    /** A globe: which language and region the interface follows. */
    val GlobeSimple: ImageVector by lazy { phosphor(PATH_GLOBE_SIMPLE, "GlobeSimple") }

    /** A magnifying glass: search within a local list. */
    val MagnifyingGlass: ImageVector by lazy {
        phosphor(PATH_MAGNIFYING_GLASS, "MagnifyingGlass")
    }

    /** A downward caret: this opens a chooser; bold, as the mock draws it. */
    val CaretDown: ImageVector by lazy { phosphor(PATH_CARET_DOWN, "CaretDown") }

    /** Three stacked dots: the controls the chrome's bars have no room for. */
    val DotsThreeVertical: ImageVector by lazy {
        phosphor(PATH_DOTS_THREE_VERTICAL, "DotsThreeVertical")
    }

    /** A lower-case i: a note about how something works. */
    val Info: ImageVector by lazy { phosphor(PATH_INFO, "Info") }

    /** A crown: the paid plan. */
    val CrownSimple: ImageVector by lazy { phosphor(PATH_CROWN_SIMPLE, "CrownSimple") }

    /** A key: the user's own provider credential, held by them. */
    val Key: ImageVector by lazy { phosphor(PATH_KEY, "Key") }

    /** A price tag: what something costs. */
    val Tag: ImageVector by lazy { phosphor(PATH_TAG, "Tag") }

    /** A rightward caret: this opens something; bold, as the mock draws it. */
    val CaretRight: ImageVector by lazy { phosphor(PATH_CARET_RIGHT, "CaretRight") }

    /** A sealed envelope: an address, or a link sent to one. */
    val EnvelopeSimple: ImageVector by lazy { phosphor(PATH_ENVELOPE_SIMPLE, "EnvelopeSimple") }

    /** A hat and glasses: identity kept apart from identity. */
    val Detective: ImageVector by lazy { phosphor(PATH_DETECTIVE, "Detective") }

    /** A film strip: media found on a page. */
    val FilmStrip: ImageVector by lazy { phosphor(PATH_FILM_STRIP, "FilmStrip") }

    /** An arrow out of a tray: take this elsewhere. */
    val Export: ImageVector by lazy { phosphor(PATH_EXPORT, "Export") }

    /** A wrapped box: something new arrives. */
    val Gift: ImageVector by lazy { phosphor(PATH_GIFT, "Gift") }

    /** A cloud: a request that leaves the device. */
    val Cloud: ImageVector by lazy { phosphor(PATH_CLOUD, "Cloud") }

    /** Two arrows in a circle: the same thing on two devices. */
    val ArrowsClockwise: ImageVector by lazy { phosphor(PATH_ARROWS_CLOCKWISE, "ArrowsClockwise") }

    /** A cloud with an arrow: a backup. */
    val CloudArrowUp: ImageVector by lazy { phosphor(PATH_CLOUD_ARROW_UP, "CloudArrowUp") }

    /** A puzzle piece: a skill, installed and revocable. */
    val PuzzlePiece: ImageVector by lazy { phosphor(PATH_PUZZLE_PIECE, "PuzzlePiece") }

    /** A ringing bell: something the assistant re-checked. */
    val BellRinging: ImageVector by lazy { phosphor(PATH_BELL_RINGING, "BellRinging") }

    /** A sun: the light appearance. */
    val Sun: ImageVector by lazy { phosphor(PATH_SUN, "Sun") }

    /** A crescent with two stars: the dark appearance. */
    val MoonStars: ImageVector by lazy { phosphor(PATH_MOON_STARS, "MoonStars") }

    /**
     * The rising sun of the theme change, and the crescent that replaces it.
     *
     * Fill rather than regular, and `handoff/DESIGN.md` section 4 permits it
     * without an edit: it assigns regular to *controls*, and the body of a
     * theme transition is not one — nothing taps it, because the overlay eats
     * every pointer event for the duration. Size settles it independently. The
     * body draws at about sixty-five units across and a hundred and ten with
     * its rays, five times the largest icon size section 4 lists, and at that
     * scale a regular glyph's strokes become hairlines and the crescent reads
     * as a ring rather than a moon.
     */
    val SunFill: ImageVector by lazy { phosphor(PATH_SUN_FILL, "SunFill") }

    val MoonStarsFill: ImageVector by lazy { phosphor(PATH_MOON_STARS_FILL, "MoonStarsFill") }

    /** A cogwheel: settings. */
    val Gear: ImageVector by lazy { phosphor(PATH_GEAR, "Gear") }

    /** A waste bin: close or discard everything here. */
    val Trash: ImageVector by lazy { phosphor(PATH_TRASH, "Trash") }

    /** A ruled grid: a workspace made from tabs. */
    val Table: ImageVector by lazy { phosphor(PATH_TABLE, "Table") }

    /** An arrow leaving the corner: this opens elsewhere. */
    val ArrowUpRight: ImageVector by lazy { phosphor(PATH_ARROW_UP_RIGHT, "ArrowUpRight") }

    /** A framed picture: the wallpaper behind a new tab. */
    val Image: ImageVector by lazy { phosphor(PATH_IMAGE, "Image") }

    /** A shelf of books: everything this browser has kept for you. */
    val Books: ImageVector by lazy { phosphor(PATH_BOOKS, "Books") }

    /** An arrow landing on a shelf: the download list. */
    val DownloadSimple: ImageVector by lazy { phosphor(PATH_DOWNLOAD_SIMPLE, "DownloadSimple") }

    /** Four squares: the workspaces a person keeps. */
    val SquaresFour: ImageVector by lazy { phosphor(PATH_SQUARES_FOUR, "SquaresFour") }

    /** A person in a circle: you. */
    val UserCircle: ImageVector by lazy { phosphor(PATH_USER_CIRCLE, "UserCircle") }

    /** A clock face: time on sites. Distinct from history. */
    val Clock: ImageVector by lazy { phosphor(PATH_CLOCK, "Clock") }

    /** An identity card: saved details. */
    val IdentificationCard: ImageVector by lazy {
        phosphor(PATH_IDENTIFICATION_CARD, "IdentificationCard")
    }

    /** A newspaper: what happened. */
    val Newspaper: ImageVector by lazy { phosphor(PATH_NEWSPAPER, "Newspaper") }

    /** A monitor: the desktop version of a site. */
    val Monitor: ImageVector by lazy { phosphor(PATH_MONITOR, "Monitor") }

    /** A star: save this page. */
    val Star: ImageVector by lazy { phosphor(PATH_STAR, "Star") }

    /** Horizontal sliders: general settings. */
    val SlidersHorizontal: ImageVector by lazy {
        phosphor(PATH_SLIDERS_HORIZONTAL, "SlidersHorizontal")
    }

    /** A leftward caret: back; bold, as the mock draws it. */
    val CaretLeft: ImageVector by lazy { phosphor(PATH_CARET_LEFT, "CaretLeft") }

    /** Two stacked pages: copy this. */
    val CopySimple: ImageVector by lazy { phosphor(PATH_COPY_SIMPLE, "CopySimple") }

    /** A character and a letter: which language the interface follows. */
    val Translate: ImageVector by lazy { phosphor(PATH_TRANSLATE, "Translate") }

    /** A funnel: ads and trackers, or a filter. */
    val Funnel: ImageVector by lazy { phosphor(PATH_FUNNEL, "Funnel") }

    /** Bars of a chart: a count that already exists. */
    val ChartBar: ImageVector by lazy { phosphor(PATH_CHART_BAR, "ChartBar") }

    /** A question mark in a circle: help. */
    val Question: ImageVector by lazy { phosphor(PATH_QUESTION, "Question") }

    /** Two letter sizes: how large the page text is. */
    val TextAa: ImageVector by lazy { phosphor(PATH_TEXT_AA, "TextAa") }

    /** A flag: which country this browser is in. */
    val Flag: ImageVector by lazy { phosphor(PATH_FLAG, "Flag") }
}

/**
 * One Phosphor glyph as an [ImageVector]: a single filled path on the 256
 * viewport the source SVGs ship with.
 */
private fun phosphor(pathData: String, name: String): ImageVector =
    ImageVector.Builder(
        name = "TaffyIcon.$name",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = Viewport,
        viewportHeight = Viewport,
    ).addPath(
        pathData = PathParser().parsePathString(pathData).toNodes(),
        fill = SolidColor(Color.Black),
    ).build()

/** The viewport of every Phosphor source SVG. */
private const val Viewport = 256f
