import {html, LitElement} from 'lit'
import {customElement, state} from 'lit/decorators.js'

/**
 * Servo Output Panel — Live PWM monitor via /pteronautos/state.
 *
 * FEATURE:PTERONAUTOS
 * Polls firmware state endpoint for live servo PWM values from Ornithopter.
 * Shows 5-channel PWMP7 table with real-time current values.
 */
@customElement('servo-panel')
class ServoPanel extends LitElement {
    @state() accessor servoLeftUs = 1500
    @state() accessor servoRightUs = 1500
    @state() accessor servoRudderUs = 1500
    @state() accessor linkUp = false
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
                this.linkUp = !!data.ornithopter.link_up
                this.servoLeftUs = Number(data.ornithopter.servo_left_us) || 1500
                this.servoRightUs = Number(data.ornithopter.servo_right_us) || 1500
                this.servoRudderUs = Number(data.ornithopter.servo_rudder_us) || 1500
            }
        } catch (e) {
            this.pollError = true
        }
    }

    _liveUs(chName) {
        if (this.pollError) return '—'
        switch(chName) {
            case 'Left Wing': return this.servoLeftUs
            case 'Right Wing': return this.servoRightUs
            case 'Crest Rudder': return this.servoRudderUs
            default: return 1500
        }
    }

    _servoChannels() {
        return [
            {ch: 1, name: 'Left Wing',    pin: 0,  center: 1500, min: 1000, max: 2000},
            {ch: 2, name: 'Right Wing',   pin: 1,  center: 1500, min: 1000, max: 2000},
            {ch: 3, name: 'Crest Rudder', pin: 3,  center: 1500, min: 1000, max: 2000},
            {ch: 4, name: 'AUX 4',        pin: 9,  center: 1500, min: 1000, max: 2000},
            {ch: 5, name: 'AUX 5',        pin: 10, center: 1500, min: 1000, max: 2000},
        ]
    }

    render() {
        const statusColor = this.pollError ? '#c84'
            : this.linkUp ? '#2d8'
            : '#888'
        const statusText = this.pollError ? 'API OFFLINE'
            : this.linkUp ? 'RC LINK ACTIVE — PWM Live'
            : 'PWMP7 — 5 PWM Outputs (No Link)'

        return html`
            <div class="mui-panel mui--text-title">
                ⚙️ Servo Output
            </div>

            <div class="mui-panel">
                <div class="badge" style="background-color: ${statusColor}; display:inline-block; padding:4px 12px; border-radius:3px;">
                    ${statusText}
                </div>
                <br><br>
                <div class="pwmtbl" style="overflow-x:auto;">
                    <table class="mui-table mui-table--bordered">
                        <thead>
                            <tr>
                                <th>CH</th>
                                <th>Name</th>
                                <th>Pin</th>
                                <th>Center</th>
                                <th>Min</th>
                                <th>Max</th>
                                <th>Failsafe</th>
                                <th>Current</th>
                            </tr>
                        </thead>
                        <tbody>
                            ${this._servoChannels().map(s => {
                                const live = this._liveUs(s.name)
                                const changed = live !== 1500
                                return html`
                                <tr>
                                    <td>${s.ch}</td>
                                    <td><b>${s.name}</b></td>
                                    <td>GPIO${s.pin}</td>
                                    <td>${s.center}µs</td>
                                    <td>${s.min}µs</td>
                                    <td>${s.max}µs</td>
                                    <td>${s.center}µs</td>
                                    <td style="color:${changed ? '#d4a017' : '#888'}; font-family:monospace;">
                                        ${live}µs
                                    </td>
                                </tr>
                                `
                            })}
                        </tbody>
                    </table>
                </div>
                <br>
                <small style="color:#888;">
                    ${this.linkUp
                        ? 'Live PWM values updating from ornithopter mixer.'
                        : 'Current values shown at neutral (1500µs). Live values update during RC link.'}
                </small>
            </div>

            <!-- Servo Test -->
            <div class="mui-panel" style="text-align:center;">
                <button class="mui-btn mui-btn--primary" disabled>
                    🔄 Sweep All Servos
                </button>
                <br>
                <small style="color:#888;">
                    Sweeps all 5 servo channels from min to max endpoints.<br>
                    <em>Coming Soon — Firmware command interface required</em>
                </small>
            </div>

            <!-- Failsafe Configuration -->
            <div class="mui-panel">
                <div class="mui--text-title" style="font-size:16px;">Failsafe Configuration</div>
                <table class="mui-table mui-table--bordered">
                    <tbody>
                        <tr>
                            <td><b>Failsafe Mode</b></td>
                            <td>
                                <select disabled>
                                    <option selected>Center (1500µs)</option>
                                    <option>Hold Last Position</option>
                                    <option>Custom Preset</option>
                                </select>
                            </td>
                        </tr>
                        <tr>
                            <td><b>Failsafe Delay</b></td>
                            <td>
                                <input type="range" min="100" max="2000" value="1000" disabled style="width:150px"/>
                                <span>1000 ms</span>
                                <br><small style="color:#888;">Time before failsafe activates after signal loss</small>
                            </td>
                        </tr>
                    </tbody>
                </table>
            </div>
        `
    }
}

export default ServoPanel
