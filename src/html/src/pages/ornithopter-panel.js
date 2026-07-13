import {html, LitElement} from 'lit'
import {customElement, state} from 'lit/decorators.js'
import {elrsState} from '../utils/state.js'

/**
 * Ornithopter Panel — Waveform, flap frequency, wing amplitude, rudder offset.
 *
 * FEATURE:PTERONAUTOS
 * Displays ornithopter flight control parameters. All controls are decorative
 * (firmware-defined) but provide a visual dashboard for the flapping wing mixer.
 */
@customElement('ornithopter-panel')
class OrnithopterPanel extends LitElement {
    @state() accessor ornithopterActive = false

    createRenderRoot() {
        return this
    }

    render() {
        // Status badge is decorative — awaiting firmware integration
        const active = this.ornithopterActive || !!elrsState.settings?.['product_name']

        return html`
            <div class="mui-panel mui--text-title">
                🦴 Ornithopter Control
            </div>
            <div class="mui-panel">
                <div class="badge" style="background-color: ${active ? '#2d8' : '#888'}; display:inline-block; padding:4px 12px;">
                    ${active ? 'ORNITHOPTER ACTIVE' : 'RC LINK REQUIRED'}
                </div>
                <br><br>
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
                                <br><small style="color:#888">Select waveform shape for flap cycle</small>
                            </td>
                        </tr>
                        <tr>
                            <td><b>Flap Frequency</b></td>
                            <td>
                                <input type="range" min="1" max="15" value="5" disabled style="width:200px"/>
                                <span>5 Hz</span>
                                <br><small style="color:#888">Wingbeat frequency (1-15 Hz)</small>
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
                                <br><small style="color:#888">Cephalic crest servo center offset</small>
                            </td>
                        </tr>
                        <tr>
                            <td><b>Throttle → Frequency Map</b></td>
                            <td>
                                <small style="color:#888">
                                    CH3 throttle linearly maps to flap frequency.<br>
                                    Min throttle = 1 Hz idle, Max throttle = configured max frequency.
                                </small>
                            </td>
                        </tr>
                    </tbody>
                </table>
            </div>
            <div class="mui-panel" style="text-align:center; color:#888; font-style:italic;">
                🦴 Full ornithopter configuration available via CRSF Lua script<br>
                <small>Coming in PteronautOS v1.1</small>
            </div>
        `
    }
}

export default OrnithopterPanel