import {LitElement} from 'lit'
import renderFn from './backup-panel.lithaml'
import {i18n} from '../utils/i18n'

###
# BackupPanel — full config export/import (options + RxConfig + ornithopter).
# GET  /pteronautos/backup → download one JSON blob
# POST /pteronautos/backup → restore that blob, then the device reboots.
###
class BackupPanel extends LitElement
  @properties:
    state:   {state: true}   # idle | working | done | error
    message: {state: true}

  constructor: ->
    super()
    @state   = 'idle'
    @message = ''

  createRenderRoot: -> this
  _t: (key, params = {}) -> i18n.t key, params

  _busy: -> @state is 'working'

  _statusStyle: ->
    switch @state
      when 'working' then 'color:#d4a017;'
      when 'done'    then 'color:#4caf50;'
      when 'error'   then 'color:#e05555;'
      else ''

  _slug: (s) ->
    return '' unless s
    String(s).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '')

  _export: ->
    return if @_busy()
    @state = 'working'
    @message = ''
    try
      res = await fetch '/pteronautos/backup'
      throw new Error "HTTP #{res.status}" unless res.ok
      text = await res.text()
      throw new Error (i18n.t 'backup.error.html') unless text.trim().startsWith('{')
      data = JSON.parse text
      model = (data.pteronautos?.model_name) or (data.meta?.model_name) or ''
      slug = @_slug model
      filename = if slug then "pteronaut-#{slug}.json" else 'pteronaut-backup.json'
      blob = new Blob [text], type: 'application/json'
      url = URL.createObjectURL blob
      a = document.createElement 'a'
      a.href = url
      a.download = filename
      document.body.appendChild a
      a.click()
      a.remove()
      URL.revokeObjectURL url
      @state = 'done'
      @message = @_t 'backup.export_done'
    catch e
      @state = 'error'
      @message = e?.message ? 'error'

  _importFile: (evt) ->
    file = evt.target.files?[0]
    evt.target.value = ''   # allow re-selecting the same file
    return unless file
    @state = 'working'
    @message = ''
    try
      text = await file.text()
      JSON.parse text   # validate locally before sending
      res = await fetch '/pteronautos/backup', {
        method: 'POST'
        headers: {'Content-Type': 'application/json'}
        body: text
      }
      throw new Error "HTTP #{res.status}" unless res.ok
      @state = 'done'
      @message = @_t 'backup.import_done'
    catch e
      @state = 'error'
      @message = e?.message ? 'error'

  render: -> renderFn this

customElements.define 'backup-panel', BackupPanel
export default BackupPanel