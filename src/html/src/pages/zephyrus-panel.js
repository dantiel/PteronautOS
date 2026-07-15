import {html, LitElement} from 'lit'
import {customElement, state} from 'lit/decorators.js'

/**
 * Zephyrus Gyro Panel — MPU6050 live telemetry via /pteronautos/state.
 *
 * FEATURE:PTERONAUTOS
 * Polls firmware state endpoint every 500ms for live gyro data.
 * Shows artificial horizon with real roll/pitch angles.
 * PID values displayed read-only (firmware-defined for now).
 */
@customElement('zephyrus-panel')
class ZephyrusPanel extends LitElement {
    @state() accessor gyroEnabled = false
    @state() accessor gyroCalibrated = false
    @state() accessor rollDeg = 0
    @state() accessor pitchDeg = 0
    @state() accessor yawRate = 0
    @state() accessor rollCorrection = 0
    @state() accessor yawCorrection = 0
    @state() accessor rudderCorrection = 0
    @state() accessor uptimeMs = 0
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
            this.uptimeMs = data.uptime_ms || 0
            if (data.zephyrus) {
                this.gyroEnabled = !!data.zephyrus.enabled
                this.gyroCalibrated = !!data.zephyrus.calibrated
                this.rollDeg = Number(data.zephyrus.roll_deg) || 0
                this.pitchDeg = Number(data.zephyrus.pitch_deg) || 0
                this.yawRate = Number(data.zephyrus.yaw_rate) || 0
                this.rollCorrection = Number(data.zephyrus.roll_correction) || 0
                this.yawCorrection = Number(data.zephyrus.yaw_correction) || 0
                this.rudderCorrection = Number(data.zephyrus.rudder_correction) || 0
            }
        } catch (e) {
            this.pollError = true
        }
    }

    _horizonStyle() {
        const rollClamped = Math.max(-45, Math.min(45, this.rollDeg))
        const pitchPx = Math.max(-40, Math.min(40, this.pitchDeg * 2))
        return `transform: rotate(${rollClamped}deg) translateY(${pitchPx}px)`
    }

    render() {
        const statusColor = this.pollError ? '#c84'
            : this.gyroEnabled ? (this.gyroCalibrated ? '#2d8' : '#da0')
            : '#c44'
        const statusText = this.pollError ? 'API OFFLINE — Check firmware'
            : this.gyroEnabled ? (this.gyroCalibrated ? 'MPU6050 ACTIVE' : 'MPU6050 DETECTED — Not calibrated')
            : 'GYRO DISABLED — Connect MPU6050'

        return html`
            <div class="mui-panel mui--text-title">
                🧭 Zephyrus Gyro Stabilization
            </div>

            <!-- Gyro Status -->
            <div class="mui-panel">
                <div class="badge" style="background-color: ${statusColor}; display:inline-block; padding:4px 12px; border-radius:3px;">
                    ${statusText}
                </div>
                <br><br>
                <table class="mui-table mui-table--bordered">
                    <tbody>
                        <tr><td><b>Sensor</b></td><td>MPU6050 (GY-521)</td></tr>
                        <tr><td><b>Bus</b></td><td>I2C (GPIO1=SDA, GPIO3=SCL)</td></tr>
                        <tr><td><b>Sample Rate</b></td><td>200 Hz</td></tr>
                        <tr><td><b>AHRS Algorithm</b></td><td>Mahony</td></tr>
                        ${this.gyroEnabled ? html`
                        <tr><td><b>Uptime</b></td><td>${Math.floor(this.uptimeMs / 1000)}s</td></tr>
                        ` : ''}
                    </tbody>
                </table>
            </div>

            <!-- Live Telemetry -->
            ${this.gyroEnabled ? html`
            <div class="mui-panel">
                <div class="mui--text-title" style="font-size:16px;">Live Telemetry</div>
                <table class="mui-table mui-table--bordered">
                    <tbody>
                        <tr><td><b>Roll</b></td><td>${this.rollDeg.toFixed(1)}°</td><td><b>Roll Corr</b></td><td>${this.rollCorrection.toFixed(1)}</td></tr>
                        <tr><td><b>Pitch</b></td><td>${this.pitchDeg.toFixed(1)}°</td><td><b>Yaw Rate</b></td><td>${this.yawRate.toFixed(1)}°/s</td></tr>
                        <tr><td><b>Yaw Corr</b></td><td>${this.yawCorrection.toFixed(1)}</td><td><b>Rudder</b></td><td>${this.rudderCorrection.toFixed(1)}</td></tr>
                    </tbody>
                </table>
            </div>
            ` : ''}

            <!-- Artificial Horizon (live-driven) -->
            <div class="mui-panel" style="text-align:center;">
                <div style="font-size:12px; color:#888; margin-bottom:8px;">Attitude Indicator</div>
                <div class="pteronautos-horizon" style="
                    width:200px; height:200px; margin:0 auto; overflow:hidden;
                    background: linear-gradient(180deg, #3a6fc2 0%, #3a6fc2 50%, #5c3d0e 50%, #6b4c1e 100%);
                    border-radius:50%; border:3px solid #d4a017; position:relative;
                ">
                    <!-- Horizon line -->
                    <div style="
                        ${this._horizonStyle()}
                        width:100%; height:2px; background:#fff;
                        position:absolute; top:50%; left:0;
                        transition: transform 0.15s ease-out;
                    ">
                        <!-- Center dot -->
                        <div style="
                            position:absolute; top:-7px; left:50%; margin-left:-7px;
                            width:14px; height:14px; background:#d4a017; border-radius:50%;
                            border:2px solid #fff;
                        "></div>
                    </div>
                </div>
                <small style="color:#888; display:block; margin-top:8px;">
                    ${this.gyroEnabled ? 'Live — firmware telemetry active' : 'Waiting for gyro connection...'}
                </small>
            </div>

            <!-- PID Parameters (read-only for now) -->
            <div class="mui-panel">
                <div class="mui--text-title" style="font-size:16px;">Roll Axis PID</div>
                <table class="mui-table mui-table--bordered">
                    <tbody>
                        <tr><td><b>P (Proportional)</b></td><td><input type="range" min="0" max="100" value="30" disabled style="width:150px"/> 0.30</td></tr>
                        <tr><td><b>I (Integral)</b></td><td><input type="range" min="0" max="100" value="5" disabled style="width:150px"/> 0.05</td></tr>
                        <tr><td><b>D (Derivative)</b></td><td><input type="range" min="0" max="100" value="15" disabled style="width:150px"/> 0.15</td></tr>
                        <tr><td><b>Max Correction</b></td><td><input type="range" min="0" max="100" value="40" disabled style="width:150px"/> 40%</td></tr>
                    </tbody>
                </table>
                <br>
                <div class="mui--text-title" style="font-size:16px;">Yaw Axis PID</div>
                <table class="mui-table mui-table--bordered">
                    <tbody>
                        <tr><td><b>P (Proportional)</b></td><td><input type="range" min="0" max="100" value="25" disabled style="width:150px"/> 0.25</td></tr>
                        <tr><td><b>I (Integral)</b></td><td><input type="range" min="0" max="100" value="3" disabled style="width:150px"/> 0.03</td></tr>
                        <tr><td><b>D (Derivative)</b></td><td><input type="range" min="0" max="100" value="10" disabled style="width:150px"/> 0.10</td></tr>
                        <tr><td><b>Max Correction</b></td><td><input type="range" min="0" max="100" value="50" disabled style="width:150px"/> 50%</td></tr>
                    </tbody>
                </table>
            </div>

            <!-- Calibrate Button -->
            <div class="mui-panel" style="text-align:center;">
                <button class="mui-btn mui-btn--primary" ?disabled=${!this.gyroEnabled}>
                    Calibrate Gyro
                </button>
                <br>
                <small style="color:#888;">
                    ${this.gyroEnabled
                        ? 'Place aircraft level and press to calibrate'
                        : 'Available when gyro is detected'}
                </small>
            </div>
        `
    }
}

export default ZephyrusPanel
