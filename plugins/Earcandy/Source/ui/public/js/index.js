import * as Juce from "./juce/index.js";

// ─── Arc Knob Constants ───
const ARC_START = 0.75 * Math.PI;   // 135° = 7:30 position (bottom-left)
const ARC_END   = 0.25 * Math.PI;   // 45°  = 4:30 position (bottom-right)
const ARC_RANGE = 1.5  * Math.PI;   // 270° sweep
const KNOB_RADIUS = 33;
const CANVAS_SIZE = 144;
const CENTER = CANVAS_SIZE / 2;

function valueToAngle(normalized) {
  return ARC_START + normalized * ARC_RANGE;
}

// ─── Canvas Knob Renderer ───
function drawKnob(canvas, normalized, isActive) {
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, CANVAS_SIZE, CANVAS_SIZE);

  // Glass body
  const glassGrad = ctx.createRadialGradient(
    CENTER - 8, CENTER - 8, 2,
    CENTER, CENTER, KNOB_RADIUS + 2
  );
  glassGrad.addColorStop(0, 'rgba(255, 255, 255, 0.95)');
  glassGrad.addColorStop(0.5, 'rgba(255, 255, 255, 0.7)');
  glassGrad.addColorStop(1, 'rgba(243, 239, 248, 0.4)');

  ctx.beginPath();
  ctx.arc(CENTER, CENTER, KNOB_RADIUS, 0, Math.PI * 2);
  ctx.fillStyle = glassGrad;
  ctx.fill();

  // Purple tint overlay
  ctx.beginPath();
  ctx.arc(CENTER, CENTER, KNOB_RADIUS, 0, Math.PI * 2);
  ctx.fillStyle = 'rgba(139, 92, 246, 0.06)';
  ctx.fill();

  // Top-left highlight (refraction)
  const highlightGrad = ctx.createRadialGradient(
    CENTER - 12, CENTER - 14, 1,
    CENTER - 8, CENTER - 10, 18
  );
  highlightGrad.addColorStop(0, 'rgba(255, 255, 255, 0.8)');
  highlightGrad.addColorStop(1, 'rgba(255, 255, 255, 0)');
  ctx.beginPath();
  ctx.arc(CENTER, CENTER, KNOB_RADIUS - 1, 0, Math.PI * 2);
  ctx.fillStyle = highlightGrad;
  ctx.fill();

  // Inner shadow
  const shadowGrad = ctx.createRadialGradient(
    CENTER, CENTER, KNOB_RADIUS - 6,
    CENTER, CENTER, KNOB_RADIUS
  );
  shadowGrad.addColorStop(0, 'rgba(0, 0, 0, 0)');
  shadowGrad.addColorStop(1, 'rgba(0, 0, 0, 0.04)');
  ctx.beginPath();
  ctx.arc(CENTER, CENTER, KNOB_RADIUS, 0, Math.PI * 2);
  ctx.fillStyle = shadowGrad;
  ctx.fill();

  // Arc track (inactive)
  ctx.beginPath();
  ctx.arc(CENTER, CENTER, KNOB_RADIUS + 5, ARC_START, ARC_END, false);
  ctx.strokeStyle = '#E8E0F0';
  ctx.lineWidth = 3;
  ctx.lineCap = 'round';
  ctx.stroke();

  // Active arc
  if (normalized > 0.003) {
    const endAngle = valueToAngle(normalized);

    // Glow when active
    if (isActive) {
      ctx.beginPath();
      ctx.arc(CENTER, CENTER, KNOB_RADIUS + 5, ARC_START, endAngle, false);
      ctx.strokeStyle = 'rgba(139, 92, 246, 0.25)';
      ctx.lineWidth = 8;
      ctx.lineCap = 'round';
      ctx.stroke();
    }

    // Active arc stroke
    ctx.beginPath();
    ctx.arc(CENTER, CENTER, KNOB_RADIUS + 5, ARC_START, endAngle, false);
    ctx.strokeStyle = isActive ? '#7C3AED' : '#8B5CF6';
    ctx.lineWidth = 3.5;
    ctx.lineCap = 'round';
    ctx.stroke();

    // Indicator dot
    const dotX = CENTER + (KNOB_RADIUS + 5) * Math.cos(endAngle);
    const dotY = CENTER + (KNOB_RADIUS + 5) * Math.sin(endAngle);
    ctx.beginPath();
    ctx.arc(dotX, dotY, 2.5, 0, Math.PI * 2);
    ctx.fillStyle = isActive ? '#7C3AED' : '#8B5CF6';
    ctx.fill();

    if (isActive) {
      ctx.beginPath();
      ctx.arc(dotX, dotY, 5, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(139, 92, 246, 0.3)';
      ctx.fill();
    }
  }
}

// ─── Knob Controller ───
class KnobController {
  constructor(groupEl, sliderState) {
    this.group = groupEl;
    this.sliderState = sliderState;
    this.canvas = groupEl.querySelector('.knob-canvas');
    this.valueEl = groupEl.querySelector('.knob-value');
    this.min = parseFloat(groupEl.dataset.min);
    this.max = parseFloat(groupEl.dataset.max);
    this.defaultVal = parseFloat(groupEl.dataset.default);
    this.decimals = parseInt(groupEl.dataset.decimals);
    this.unit = groupEl.dataset.unit;
    this.paramId = groupEl.dataset.param;
    this.isActive = false;
    this.dragStartY = 0;
    this.dragStartNorm = 0;

    // Listen for C++ automation changes
    if (this.sliderState) {
      this.sliderState.valueChangedEvent.addListener(() => {
        const norm = this.sliderState.getNormalisedValue();
        this.updateFromNormalized(norm, false);
      });
    }

    // Mouse interaction
    this.group.addEventListener('mousedown', (e) => this.onMouseDown(e));
    this.group.addEventListener('dblclick', (e) => this.onDoubleClick(e));

    // Initial draw
    this.updateFromNormalized(this.valueToNormalized(parseFloat(groupEl.dataset.value)), false);
  }

  valueToNormalized(val) {
    return (val - this.min) / (this.max - this.min);
  }

  normalizedToValue(norm) {
    return this.min + norm * (this.max - this.min);
  }

  updateFromNormalized(norm, notifyHost) {
    norm = Math.max(0, Math.min(1, norm));
    const val = this.normalizedToValue(norm);
    this.group.dataset.value = val.toFixed(this.decimals);

    const sign = val > 0 && this.paramId === 'PITCH' ? '+' : '';
    this.valueEl.textContent = sign + val.toFixed(this.decimals) + this.unit;

    drawKnob(this.canvas, norm, this.isActive);

    if (notifyHost && this.sliderState) {
      this.sliderState.setNormalisedValue(norm);
    }
  }

  onMouseDown(e) {
    e.preventDefault();
    this.isActive = true;
    this.dragStartY = e.clientY;
    this.dragStartNorm = this.valueToNormalized(parseFloat(this.group.dataset.value));
    this.group.classList.add('dragging');

    if (this.sliderState) this.sliderState.sliderDragStarted();

    drawKnob(this.canvas, this.dragStartNorm, true);

    const onMove = (ev) => {
      const sensitivity = ev.shiftKey ? 800 : 200;
      const deltaY = this.dragStartY - ev.clientY;
      const deltaNorm = deltaY / sensitivity;
      this.updateFromNormalized(this.dragStartNorm + deltaNorm, true);
    };

    const onUp = () => {
      this.isActive = false;
      this.group.classList.remove('dragging');
      if (this.sliderState) this.sliderState.sliderDragEnded();
      const norm = this.valueToNormalized(parseFloat(this.group.dataset.value));
      drawKnob(this.canvas, norm, false);
      document.removeEventListener('mousemove', onMove);
      document.removeEventListener('mouseup', onUp);
    };

    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onUp);
  }

  onDoubleClick(e) {
    e.preventDefault();
    const norm = this.valueToNormalized(this.defaultVal);
    this.updateFromNormalized(norm, true);
  }
}

// ─── Initialize ───
document.addEventListener("DOMContentLoaded", () => {
  const knobGroups = document.querySelectorAll('.knob-group');

  knobGroups.forEach(group => {
    const paramId = group.dataset.param;
    let sliderState = null;

    try {
      sliderState = Juce.getSliderState(paramId);
    } catch (e) {
      // Running in browser preview without JUCE backend
      console.log(`Preview mode: no JUCE backend for ${paramId}`);
    }

    new KnobController(group, sliderState);
  });

  console.log("Earcandy WebView UI initialized");
});
