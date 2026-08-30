import {LitElement} from 'lit'
import renderFn from './app.lithaml'
import {initRipple} from './utils/ripple'
import {initMuiSelect} from './utils/select'
import {elrsState, formatBand} from './utils/state'
import {overlay} from './utils/overlay'
import './components/elrs-footer'
import {i18n} from './utils/i18n-loader'
import './pages/info-panel.coffee'
import {hideLoadingOverlay, loadJSON, showConfirm, showLoadingOverlay} from './utils/feedback'
import {_} from './utils/libs'

# PteronautOS: static imports — panels bundled into app.js (no chunk HTTP, no ESP crash)
import './page-groups/general-group.coffee'
import './page-groups/advanced-group.coffee'

SETTINGS_LOAD_FAILED_MESSAGE = 'Failed to load settings. Retry or power cycle device.'

# Static brand glyph (pterosaur ASCII art). CoffeeScript heredocs process
# backslash escapes, so every literal backslash below is doubled (\\ → \).
BRAND_GLYPH = """
     ___
    /    \\___
   /  _     o\\___
  /  /  \\___  /    \\
 |  /   \\     \\/  _  \\
 | /     \\    /  / \\  |
  \\_____/\\/  /   \\  |
        /\\___/     \\ |
       /
"""

GLYPH_ARCHAEOPTERYX = """
                           _
                        __~a~_
                        ~~;  ~_
          _                ~  ~_                _
         '_\;__._._._._._._]   ~_._._._._._.__;/_`
         '(/'/'/'/'|'|'|'| (    )|'|'|'|'\'\'\'\)'
         (/ / / /, | | | |(/    \) | | | ,\ \ \ \)
        (/ / / / / | | | ~(/    \) ~ | | \ \ \ \ \)
       (/ / / / /  ~ ~ ~   (/  \)    ~ ~  \ \ \ \ \)
      (/ / / / ~          / (||)|          ~ \ \ \ \)
      ~ / / ~            M  /||\M             ~ \ \ ~
       ~ ~                  /||\                 ~ ~
                           //||\\
                           //||\\
                           //||\\
                           '/||\'        
"""



GLYPH_PTEROSAUR_1 = """
⠀⠀⠀⠀⠀⣠⣤⣤⣤⡀⠀⠐⢶⣤⣤⣀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⢀⣴⣿⣿⣿⣿⣿⣿⣦⠀⠀⠈⠛⠿⣿⣷⣶⣿⣷⣶⡶⠶⠒⠂⠀⠀⠀⠀
⠀⢀⡾⠿⠿⠿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠉⢿⣿⣯⣤⡶⠖⠋⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠙⣿⣿⣿⣦⠀⠀⠀⠀⣠⣶⣄⠉⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠘⣿⣿⣿⣿⣦⣤⣾⣿⣿⠇⠀⠀⣀⣀⣤⣴⣶⣦⡀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⢹⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⡀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢿⣿⣿⣿⣿⣿⣿⢻⣿⣿⡿⠟⠛⠛⠿⢿⣿⣿⡇⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣩⡍⠉⠙⠋⠰⠟⠋⠁⠀⠀⠀⠀⠀⠀⠈⠻⡇⠀
⠀⠀⠀⠀⠀⠀⠀⠠⣄⣠⡾⠛⢀⣼⠏⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠁⠀
⠀⠀⠀⠀⠀⠀⠀⠒⢛⠿⢴⣶⡟⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠈⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
""" 


GLYPH_PTEROSAUR_2 = """
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⠟⠛⠛⠛⢿⣿⣯⡉⠛⠛⠿⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⡿⠋⠀⠀⠀⠀⠀⠀⠙⣿⣿⣷⣤⣀⠀⠈⠉⠀⠈⠉⢉⣉⣭⣽⣿⣿⣿⣿
⣿⡿⢁⣀⣀⣀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣶⡀⠀⠐⠛⢉⣩⣴⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣦⠀⠀⠀⠙⣿⣿⣿⣿⠟⠉⠻⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣧⠀⠀⠀⠀⠙⠛⠁⠀⠀⣸⣿⣿⠿⠿⠛⠋⠉⠙⢿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⡆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⢿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡀⠀⠀⠀⠀⠀⠀⡄⠀⠀⢀⣠⣤⣤⣀⡀⠀⠀⢸⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠖⢲⣶⣦⣴⣏⣠⣴⣾⣿⣿⣿⣿⣿⣿⣷⣄⢸⣿
⣿⣿⣿⣿⣿⣿⣿⣟⠻⠟⢁⣤⡿⠃⣰⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣾⣿
⣿⣿⣿⣿⣿⣿⣿⣭⡤⣀⡋⠉⢠⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣷⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
"""



GLYPH_PTEROSAUR_3 = '''
                                 <\\              _
                                  \\\\          _/{
                           _       \\\\       _-   -_
                         /{        / `\\   _-     - -_
                       _~  =      ( @  \\ -        -  -_
                     _- -   ~-_   \\( =\\ \\           -  -_
                   _~  -       ~_ | 1 :\\ \\      _-~-_ -  -_
                 _-   -          ~  |V: \\ \\  _-~     ~-_-  -_
              _-~   -            /  | :  \\ \\            ~-_- -_
           _-~    -   _.._      {   | : _-``               ~- _-_
        _-~   -__..--~    ~-_  {   : \\:}
      =~__.--~~              ~-_\\  :  /
                                 \\ : /__
                                //`Y'--\\\\
                               <+       \\\\
                                \\\\      WWW      
                                '
'''

GLYPH_PTEROSAUR_4 = """
    -\-
    \-- \-
     \  - -\
      \      \\
       \       \
        \       \\
         \        \\
         \          \\
         \           \\\
          \            \\
           \            \\
           \. .          \\
            \    .       \\
             \      .    \\
              \       .  \\
              \         . \\
              \            <=)
              \            <==)
              \            <=)
               \           .\\                                           _-
               \         .   \\                                        _-//
               \       .     \\                                     _-_/ /
               \ . . .        \\                                 _--_/ _/
                \              \\                              _- _/ _/
                \               \\                      ___-(O) _/ _/
                \                \                  __--  __   /_ /      ***********************************
                \                 \\          ____--__----  /    \_       I AM A MOTHERFUCKING PTERODACTYL
                 \                  \\       -------       /   \_  \_     HERE TO PTERO-YOU A NEW ASSHOLE
                  \                   \                  //   // \__ \_   **********************************
                   \                   \\              //   //      \_ \_
                    \                   \\          ///   //          \__-
                    \                -   \\/////////    //
                    \            -         \_         //
                    /        -                      //
                   /     -                       ///
                  /   -                       //
             __--/                         ///
  __________/                            // |
//-_________      ___                ////  |
        ____\__--/                /////    |
   -----______    -/---________////        |
     _______/  --/    \                   |
   /_________-/       \                   |
  //                  \                   /
                       \.                 /
                       \     .            /
                        \       .        /
                       \\           .    /
                        \                /
                        \              __|
                        \              ==/
                        /              //
                        /          .  //
                        /   .  .    //
                       /.           /
                      /            //
                      /           /
                     /          //
                    /         //
                 --/         /
                /          //
            ////         //
         ///_________////
"""

###
# App — PteronautOS WebUI shell.
# Routing, sidedrawer, locale switcher and the initial settings load flow.
# Render template lives in app.lithaml.
###
class App extends LitElement
  @properties:
    currentRoute: {state: true}

  removeRippleListeners: null
  removeSelectListeners: null
  sidedrawerClosePromise: null
  sideDrawer: null
  mainEl: null

  generalGroupLoaded: true
  advancedGroupLoaded: true

  constructor: ->
    super()
    @currentRoute = null
    @_onLocaleChange = => @requestUpdate()
    window.addEventListener 'locale-changed', @_onLocaleChange

  _t: (key, params = {}) -> i18n.t key, params

  _onLangSelect: (e) => i18n.setLocale e.target.value

  # — Lithaml template helpers —
  _brandGlyph: -> GLYPH_PTEROSAUR_1 #BRAND_GLYPH
  _locales: -> i18n.availableLocales
  _isSelectedLocale: (code) -> i18n.locale is code
  _hasConnections: -> elrsState.config.pwm isnt undefined
  _hasVoltage: -> (elrsState.settings?.voltage_source_count ? 0) > 0
  _productName: -> elrsState.settings?.product_name
  _version: -> elrsState.settings?.version
  _target: -> elrsState.settings?.target
  _band: -> formatBand()

  createRenderRoot: -> this

  render: -> renderFn this

  # — Lifecycle wiring and teardown —
  firstUpdated: ->
    @removeRippleListeners = initRipple()
    @removeSelectListeners = initMuiSelect()
    @sideDrawer = _ 'sidedrawer'
    @mainEl = @querySelector '#main'
    window.addEventListener 'hashchange', @renderRoute
    @initializeApp()

  disconnectedCallback: ->
    if @_onLocaleChange
      window.removeEventListener 'locale-changed', @_onLocaleChange
      @_onLocaleChange = null
    @removeRippleListeners?()
    @removeRippleListeners = null
    @removeSelectListeners?()
    @removeSelectListeners = null
    window.removeEventListener 'hashchange', @renderRoute
    super.disconnectedCallback()

  # — Initial data loading and retry flow —
  initializeApp: ->
    loaded = await @runWithSettingsRetry 'Loading settings...', => @loadInitialData()
    @renderRoute() if loaded

  runWithSettingsRetry: (loadingMessage, operation) ->
    while true
      await showLoadingOverlay loadingMessage
      try
        return await operation()
      catch error
        hideLoadingOverlay()
        result = await showConfirm 'Settings Load Failed', SETTINGS_LOAD_FAILED_MESSAGE, 'Retry', 'Close'
        return false if result isnt 'confirm'
      finally
        hideLoadingOverlay()

  loadInitialData: ->
    data = await loadJSON '/config', 'Failed to load config'
    elrsState.settings = data.settings or {}
    elrsState.options = data.options or {}
    elrsState.config = data.config or {}
    document.title = 'PteronautOS ' + (data.settings['module-type'] or 'RX') + ' WebUI'
    @requestUpdate()
    true

  # — UI utilities and drawer DOM helpers —
  scrollMainToTop: ->
    doScroll = (behavior = 'smooth') ->
      try
        window.scrollTo {top: 0, left: 0, behavior}
      catch
        window.scrollTo 0, 0
    requestAnimationFrame -> requestAnimationFrame -> doScroll 'smooth'

  getOverlayElement: -> document.getElementById 'mui-overlay'

  teardownSidedrawer: ->
    if @sideDrawer and not @contains @sideDrawer
      @appendChild @sideDrawer
    @sideDrawer?.classList.remove 'active'

  setActiveMenu: (route) ->
    if @sideDrawer
      links = @sideDrawer.querySelectorAll 'a[href^="#"]'
      links.forEach (a) -> a.classList.remove 'active'
    cleanRoute = route.replace /^\/+/, ''
    id = 'menu-' + cleanRoute
    el = if id then (@querySelector("##{id}") or document.getElementById id) else null
    el.classList.add 'active' if el

  loadGeneralGroup: -> Promise.resolve()
  loadAdvancedGroup: -> Promise.resolve()

  ensureLoadedForRoute: (route) ->
    # PteronautOS: all panels are statically imported and bundled into app.js.
    # No runtime chunk loading — avoids ESP heap pressure and extra HTTP round-trips.
    Promise.resolve()

  # — Transition/animation timing helpers —
  waitForElementTransition: (element, mutate, fallbackMs = 220) ->
    new Promise (resolve) ->
      unless element
        resolve()
        return

      settled = false
      fallbackTimer = null
      finish = ->
        return if settled
        settled = true
        element.removeEventListener 'transitionend', onEnd
        if fallbackTimer isnt null
          clearTimeout fallbackTimer
          fallbackTimer = null
        resolve()

      onEnd = (event) ->
        return if event?.target isnt element
        finish()

      element.addEventListener 'transitionend', onEnd
      try
        mutate?()
      catch
        finish()
        return
      fallbackTimer = setTimeout finish, fallbackMs

  animateMainIn: ->
    new Promise (resolve) =>
      unless @mainEl
        resolve()
        return

      @mainEl.classList.add 'route-fade-in'
      requestAnimationFrame =>
        @mainEl.classList.remove 'route-fade-out'
        requestAnimationFrame =>
          @mainEl.classList.remove 'route-fade-in'
          resolve()

  # — Route navigation orchestration —
  renderRoute: =>
    route = (location.hash or '#info').replace('#', '').replace /^\/+/, ''
    if @currentRoute and route is @currentRoute
      @setActiveMenu route
      return Promise.resolve()

    checkNavGuard = =>
      currentEl = @mainEl?.firstElementChild
      unless currentEl?.checkChanged?()
        return Promise.resolve true
      showConfirm('Configuration Changed', 'Do you wish to navigate away and discard changes to this page?', 'Discard', 'Cancel')
        .then((r) -> r is 'confirm')
        .catch(-> true)

    checkNavGuard().then (canNavigate) =>
      unless canNavigate
        hash = '#' + @currentRoute
        if @currentRoute and @currentRoute isnt route and hash isnt location.hash
          location.hash = hash
        @setActiveMenu (@currentRoute or route)
        return

      @setActiveMenu route
      @closeSidedrawer()
        .then(=> @ensureLoadedForRoute route)
        .then =>
          rendered = false
          @runWithSettingsRetry 'Loading panel data...', =>
            first = if rendered or not @currentRoute
              Promise.resolve()
            else
              @waitForElementTransition @mainEl, => @mainEl.classList.add 'route-fade-out'
            first
              .then => rendered or ((@currentRoute = route) and @updateComplete)
              .then => rendered or ((rendered = true) and @animateMainIn())
              .then => @mainEl?.firstElementChild?.pageReady?()
              .then =>
                @scrollMainToTop()
                true

  # — Sidedrawer interactions (mobile and desktop toggle) —
  showSidedrawer: =>
    if @sidedrawerClosePromise
      @sidedrawerClosePromise.then => @showSidedrawer()
      return

    return if @getOverlayElement()
    return unless @sideDrawer

    # Ensure known closed baseline before animating in
    @sideDrawer.classList.remove 'active'

    options =
      static: true
      keyboard: false
      onclose: => @teardownSidedrawer()

    overlayEl = overlay 'on', options
    overlayEl.appendChild @sideDrawer

    overlayEl.addEventListener 'click', (event) =>
      if event.target is overlayEl
        @closeSidedrawer()

    requestAnimationFrame => requestAnimationFrame => @sideDrawer.classList.add 'active'

  closeSidedrawer: ->
    if @sidedrawerClosePromise
      return @sidedrawerClosePromise

    overlayEl = @getOverlayElement()
    document.body.classList.remove 'hide-sidedrawer'

    # If no overlay, ensure drawer is reset and return
    unless overlayEl
      @teardownSidedrawer()
      return Promise.resolve()

    @sidedrawerClosePromise = @waitForElementTransition(@sideDrawer, =>
      @sideDrawer?.classList.remove 'active'
    ).then ->
      overlay 'off'
    .finally =>
      @sidedrawerClosePromise = null

    @sidedrawerClosePromise

  hideSidedrawer: =>
    document.body.classList.toggle 'hide-sidedrawer'

customElements.define 'elrs-app', App
export default App