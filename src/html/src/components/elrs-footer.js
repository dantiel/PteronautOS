import {html, LitElement} from 'lit'
import {customElement} from "lit/decorators.js";

@customElement('elrs-footer')
export class ElrsFooter extends LitElement {
    createRenderRoot() {
        return this
    }

    render() {
        return html`
            <footer id="footer" class="elrs-header">
                // FEATURE:PTERONAUTOS
                <div class="pteronautos-glyph">
     ___
    /    \\___
   /  _     o\\___
  /  /  \\___  /    \\
 |  /   \\     \\/  _  \\
 | /     \\    /  / \\  |
  \\_____/\\/  /   \\  |
        /\\___/     \\ |
       /
                </div>
                <div class="elrs-footer-links" style="font-size:11px; padding:2px 0;">
                    PteronautOS — Firmware para ornitóptero biomecânico
                </div>
                // /FEATURE:PTERONAUTOS
                // FEATURE:NOT PTERONAUTOS
                <div class="elrs-footer-links">
                    ExpressLRS
                </div>
                // /FEATURE:NOT PTERONAUTOS
            </footer>
        `
    }
}