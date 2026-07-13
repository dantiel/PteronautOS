import {html, LitElement} from 'lit'
import {customElement, state} from 'lit/decorators.js'
import {elrsState} from '../utils/state.js'

/**
 * Zephyrus Gyro Panel — MPU6050 status, PID tuning, artificial horizon.
 *
 * FEATURE:PTERONAUTOS
 * Shows Zephyrus gyro module connection status and PID parameters.
 * Artificial horizon is a CSS-rendered placeholder (decorative, not live).
 * PID sliders are decorative — firmware-defined parameters.
 */
@customElement('zephyrus-panel')
class ZephyrusPanel extends LitElement {
    @state() accessor gyroDetected = false // updated from elrsState.settings.zephyrus_attached on load

    createRenderRoot() {
        return this
    }

    render() {
        const detected = elrsState.settings?.zephyrus_attached !== false

        return html`
            <div class="mui-panel mui--text-title">
                🧭 Zephyrus Gyro Stabilization
            </div>

            <!-- Gyro Status -->
            <div class="mui-panel">
                <div class="badge" style="background-color: ${detected ? '#2d8' : '#c44'}; display:inline-block; padding:4px 12px;">
                    ${detected ? 'MPU6050 DETECTED' : 'GYRO DISABLED — Connect MPU6050'}
                </div>
                <br><br>
                ${detected ? html`
                    <table class="mui-table mui-table--bordered">
                        <tbody>
                            <tr><td><b>Sensor</b></td><td>MPU6050 (GY-521)</td></tr>
                            <tr><td><b>Bus</b></td><td>I2C (GPIO5=SDA, GPIO2=SCL)</td></tr>
                            <tr><td><b>Sample Rate</b></td><td>200 Hz</td></tr>
                            <tr><td><b>AHRS Algorithm</b></td><td>Mahony</td></tr>
                        </tbody>
                    </table>
                ` : html`
                    <p style="color:#888;">
                        Connect an MPU6050 gyroscope module to the I2C bus to enable
                        crest rudder stabilization via Zephyrus.
                    </p>
                    <p style="color:#666;">
                        <b>Wiring:</b> VCC→3.3V, GND→GND, SCL→GPIO2, SDA→GPIO5
                    </p>
                `}
            </div>

            <!-- Artificial Horizon (CSS decorative) -->
            <div class="mui-panel" style="text-align:center;">
                <div style="font-size:12px; color:#888; margin-bottom:8px;">Attitude Indicator</div>
                <div class="pteronautos-horizon" style="
                    width:180px; height:180px; margin:0 auto;
                    display:flex; align-items:center; justify-content:center;
                ">
                    <div style="
                        width:80%; height:2px; background:#fff;
                        transform:rotate(-3deg);
                        position:relative;
                    ">
                        <div style="
                            position:absolute; top:-6px; left:50%; margin-left:-5px;
                            width:10px; height:10px; background:#d4a017; border-radius:50%;
                        "></div>
                    </div>
                </div>
                <small style="color:#666; display:block; margin-top:8px;">
                    Live attitude visualization — firmware telemetry required<br>
                    <em>Coming in PteronautOS v1.2</em>
                </small>
            </div>

            <!-- PID Parameters -->
            <div class="mui-panel">
                <div class="mui--text-title" style="font-size:16px;">Roll Axis PID</div>
                <table class="mui-table mui-table--bordered">
                    <tbody>
                        <tr>
                            <td><b>P (Proportional)</b></td>
                            <td><input type="range" min="0" max="100" value="30" disabled style="width:150px"/> 0.30</td>
                        </tr>
                        <tr>
                            <td><b>I (Integral)</b></td>
                            <td><input type="range" min="0" max="100" value="5" disabled style="width:150px"/> 0.05</td>
                        </tr>
                        <tr>
                            <td><b>D (Derivative)</b></td>
                            <td><input type="range" min="0" max="100" value="15" disabled style="width:150px"/> 0.15</td>
                        </tr>
                        <tr>
                            <td><b>Max Correction</b></td>
                            <td><input type="range" min="0" max="100" value="40" disabled style="width:150px"/> 40%</td>
                        </tr>
                    </tbody>
                </table>
                <br>
                <div class="mui--text-title" style="font-size:16px;">Yaw Axis PID</div>
                <table class="mui-table mui-table--bordered">
                    <tbody>
                        <tr>
                            <td><b>P (Proportional)</b></td>
                            <td><input type="range" min="0" max="100" value="25" disabled style="width:150px"/> 0.25</td>
                        </tr>
                        <tr>
                            <td><b>I (Integral)</b></td>
                            <td><input type="range" min="0" max="100" value="3" disabled style="width:150px"/> 0.03</td>
                        </tr>
                        <tr>
                            <td><b>D (Derivative)</b></td>
                            <td><input type="range" min="0" max="100" value="10" disabled style="width:150px"/> 0.10</td>
                        </tr>
                        <tr>
                            <td><b>Max Correction</b></td>
                            <td><input type="range" min="0" max="100" value="50" disabled style="width:150px"/> 50%</td>
                        </tr>
                    </tbody>
                </table>
            </div>

            <!-- Calibrate Button -->
            <div class="mui-panel" style="text-align:center;">
                <button class="mui-btn mui-btn--primary" disabled>
                    Calibrate Gyro
                </button>
                <br>
                <small style="color:#888;">Place aircraft level and press to calibrate<br><em>Available when gyro is detected</em></small>
            </div>
        `
    }
}

export default ZephyrusPanel