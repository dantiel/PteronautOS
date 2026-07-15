import {html, LitElement} from 'lit'
import {customElement, state} from 'lit/decorators.js'

/**
 * Ornithopter Panel — Live ornithopter telemetry via /pteronautos/state.
 *
 * FEATURE:PTERONAUTOS
 * Polls firmware state endpoint every 500ms for live servo PWM values,
 * link status, and CRSF channel inputs.
 */
@customElement('ornithopter-panel')
class OrnithopterPanel extends LitElement {
    @state() accessor orniEnabled = false
    @state() accessor linkUp = false
    @state() accessor servoLeftUs = 1500
    @state() accessor servoRightUs = 1500
    @state() accessor servoRudderUs = 1500
    @state() accessor voiceThrottle = 0
    @state() accessor voiceCadence = 0
    @state() accessor voiceAileron = 0
    @state() accessor voiceElevator = 0
    @state() accessor voiceRudder = 0
    @state() accessor pollError = false
    @state() accessor _interval = null

    createRenderRoot() {
        return this
    }

    connectedCallback() {
        super.connectedCallback()
        this._poll()
        this._interval = setInterval(() => this._poll(), 500)
    }

    disconnectedCallback() {
        super.disconnectedCallback()
        if (this._interval) {
            clearInterval(this._interval)
            this._interval = null
        }
    }

    async _poll() {
        try {
            const resp = await fetch('/pteronautos/state')
            if (!resp.ok) throw new Error(`HTTP ${resp.status}`)
            const data = await resp.json()
            this.pollError = false
            if (data.ornithopter) {
                this.orniEnabled = !!data.ornithopter.enabled
                this.linkUp = !!data.ornithopter.link_up
                this.servoLeftUs = Number(data.ornithopter.servo_left_us) || 1500
                this.servoRightUs = Number(data.ornithopter.servo_right_us) || 1500
                this.servoRudderUs = Number(data.ornithopter.servo_rudder_us) || 1500
                this.voiceThrottle = Number(data.ornithopter.voice_throttle) || 0
                this.voiceCadence = Number(data.ornithopter.voice_cadence) || 0
                this.voiceAileron = Number(data.ornithopter.voice_aileron) || 0
                this.voiceElevator = Number(data.ornithopter.voice_elevator) || 0
                this.voiceRudder = Number(data.ornithopter.voice_rudder) || 0
            }
        } catch (e) {
            this.pollError = true
        }
    }

    _usToPercent(us) {
        // 1000us = -100%, 1500us = 0%, 2000us = +100%
        return (((us - 1500) / 500) * 100).toFixed(0)
    }

    _servoBar(us, label) {
        const pct = ((us - 1000) / 1000) * 100 // 0-100% across 1000-2000us
        const clamped = Math.max(0, Math.min(100, pct))
        const color = us > 1550 ? '#d4a017' : us < 1450 ? '#5c3d0e' : '#888'
        return html`
            <tr>
                <td><b>${label}</b></td>
                <td style="width:120px;">
                    <div style="background:#333; height:8px; border-radius:4px; position:relative;">
                        <div style="background:${color}; height:8px; border-radius:4px; width:${clamped}%; transition:width 0.15s ease-out;"></div>
                    </div>
                </td>
                <td style="font-family:monospace;">${us} µs</td>
                <td style="font-size:11px; color:#888;">${this._usToPercent(us)}%</td>
            </tr>
        `
    }

    render() {
        const statusColor = this.pollError ? '#c84'
            : this.linkUp ? '#2d8'
            : '#888'
        const statusText = this.pollError ? 'API OFFLINE'
            : this.linkUp ? 'RC LINK ACTIVE — Ornithopter Online'
            : 'RC LINK REQUIRED'

        return html`
            <div class="mui-panel mui--text-title">
                🦴 Ornithopter Control
            </div>

            <!-- Status -->
            <div class="mui-panel">
                <div class="badge" style="background-color: ${statusColor}; display:inline-block; padding:4px 12px; border-radius:3px;">
                    ${statusText}
                </div>
                <br><br>

                <!-- Servo PWM Bars -->
                <table class="mui-table mui-table--bordered">
                    <tbody>
                        ${this._servoBar(this.servoLeftUs, 'Left Wing')}
                        ${this._servoBar(this.servoRightUs, 'Right Wing')}
                        ${this._servoBar(this.servoRudderUs, 'Crest Rudder')}
                    </tbody>
                </table>
            </div>

            <!-- CRSF Channel Inputs -->
            ${this.linkUp ? html`
            <div class="mui-panel">
                <div class="mui--text-title" style="font-size:16px;">CRSF Channels</div>
                <table class="mui-table mui-table--bordered">
                    <tbody>
                        <tr><td><b>Throttle</b></td><td>${this.voiceThrottle}</td></tr>
                        <tr><td><b>Cadence</b></td><td>${this.voiceCadence}</td></tr>
                        <tr><td><b>Aileron</b></td><td>${this.voiceAileron}</td></tr>
                        <tr><td><b>Elevator</b></td><td>${this.voiceElevator}</td></tr>
                        <tr><td><b>Rudder</b></td><td>${this.voiceRudder}</td></tr>
                    </tbody>
                </table>
            </div>
            ` : ''}

            <!-- Static Configuration (decorative, firmware-defined) -->
            <div class="mui-panel">
                <div class="mui--text-title" style="font-size:16px;">Configuration</div>
                <table class="mui-table mui-table--bordered">
                    <tbody>
                        <tr>
                            <td><b>Waveform Type</b></td>
                            <td>
                                <select disabled>
                                    <option selected>Sinusoidal</option>
                                    <option>Triangular</option>
                                    <option>Trapezoidal</option>
                                </select>
                            </td>
                        </tr>
                        <tr>
                            <td><b>Flap Frequency</b></td>
                            <td>
                                <input type="range" min="1" max="15" value="5" disabled style="width:200px"/>
                                <span>5 Hz</span>
                            </td>
                        </tr>
                        <tr>
                            <td><b>Left Wing Amplitude</b></td>
                            <td>
                                <input type="range" min="0" max="100" value="80" disabled style="width:200px"/>
                                <span>80%</span>
                            </td>
                        </tr>
                        <tr>
                            <td><b>Right Wing Amplitude</b></td>
                            <td>
                                <input type="range" min="0" max="100" value="80" disabled style="width:200px"/>
                                <span>80%</span>
                            </td>
                        </tr>
                        <tr>
                            <td><b>Crest Rudder Offset</b></td>
                            <td>
                                <input type="range" min="-100" max="100" value="0" disabled style="width:200px"/>
                                <span>0 µs</span>
                            </td>
                        </tr>
                    </tbody>
                </table>
            </div>

            <div class="mui-panel" style="text-align:center; color:#888; font-style:italic;">
                🦴 Full ornithopter configuration via CRSF Lua script<br>
                <small>Coming in PteronautOS v1.1</small>
            </div>
        `
    }
}

export default OrnithopterPanel
