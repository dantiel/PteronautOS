import {html, LitElement} from 'lit'
import {customElement, state} from 'lit/decorators.js'
import {elrsState} from '../utils/state.js'

/**
 * Servo Output Panel — PWM channel monitor, center/range config, failsafe.
 *
 * FEATURE:PTERONAUTOS
 * Displays servo PWM output channels with center pulse width (default 1500µs),
 * range (1000-2000µs), failsafe position, and endpoint calibration.
 * Test button is a placeholder — firmware command not yet implemented.
 */
@customElement('servo-panel')
class ServoPanel extends LitElement {
    @state() accessor servoTestActive = false

    createRenderRoot() {
        return this
    }

    _servoChannels() {
        // PWMP7: 5 PWM outputs [0,1,3,9,10]
        // Ornithopter uses first 3 channels: Left Wing, Right Wing, Crest Rudder
        return [
            {ch: 1, name: 'Left Wing',    pin: 0,  center: 1500, min: 1000, max: 2000, current: 1500},
            {ch: 2, name: 'Right Wing',   pin: 1,  center: 1500, min: 1000, max: 2000, current: 1500},
            {ch: 3, name: 'Crest Rudder', pin: 3,  center: 1500, min: 1000, max: 2000, current: 1500},
            {ch: 4, name: 'AUX 4',        pin: 9,  center: 1500, min: 1000, max: 2000, current: 1500},
            {ch: 5, name: 'AUX 5',        pin: 10, center: 1500, min: 1000, max: 2000, current: 1500},
        ]
    }

    render() {
        return html`
            <div class="mui-panel mui--text-title">
                ⚙️ Servo Output
            </div>

            <div class="mui-panel">
                <div class="badge" style="background-color: #888; display:inline-block; padding:4px 12px;">
                    PWMP7 — 5 PWM Outputs
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
                            ${this._servoChannels().map(s => html`
                                <tr>
                                    <td>${s.ch}</td>
                                    <td><b>${s.name}</b></td>
                                    <td>GPIO${s.pin}</td>
                                    <td>${s.center}µs</td>
                                    <td>${s.min}µs</td>
                                    <td>${s.max}µs</td>
                                    <td>${s.center}µs</td>
                                    <td style="color:#d4a017;">${s.current}µs</td>
                                </tr>
                            `)}
                        </tbody>
                    </table>
                </div>
                <br>
                <small style="color:#888;">
                    Current values shown at neutral (1500µs). Live values update during RC link.
                </small>
            </div>

            <!-- Servo Test -->
            <div class="mui-panel" style="text-align:center;">
                <button class="mui-btn mui-btn--primary" disabled
                        @click=${this._onServoTest}>
                    🔄 Sweep All Servos
                </button>
                <br>
                <small style="color:#888;">
                    Sweeps all 5 servo channels from min to max endpoints.<br>
                    <em>Available when RC link is active — Coming Soon</em>
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

    _onServoTest() {
        this.servoTestActive = !this.servoTestActive
    }
}

export default ServoPanel
