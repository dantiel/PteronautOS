SVG_NS = 'http://www.w3.org/2000/svg'
PI = Math.PI
TAU = PI * 2

clamp = (value, minimum, maximum) -> Math.max minimum, Math.min maximum, value

svgElement = (name, attributes = {}) ->
  element = document.createElementNS SVG_NS, name
  for own key, value of attributes
    element.setAttribute key, value
  element

appendText = (parent, value, attributes = {}) ->
  element = svgElement 'text', attributes
  element.textContent = value
  parent.appendChild element
  element

class FerocityWaveformExplorer
  constructor: (@root) ->
    @state = down: 8, up: 0, mix: 100
    @controls = {}
    for name in ['down', 'up', 'mix']
      @controls[name] = @root.querySelector "[data-control='#{name}']"
      @controls[name].addEventListener 'input', (event) =>
        @state[event.currentTarget.dataset.control] = Number event.currentTarget.value
        @update()

    for button in @root.querySelectorAll '[data-preset]'
      button.addEventListener 'click', (event) =>
        [down, up, mix] = (Number value for value in event.currentTarget.dataset.preset.split ',' )
        @state = {down, up, mix}
        @controls.down.value = down
        @controls.up.value = up
        @controls.mix.value = mix
        @update()

    @lastWidth = Math.round @root.getBoundingClientRect().width
    @resizePending = false
    @resizeObserver = new ResizeObserver (entries) =>
      width = Math.round entries[0].contentRect.width
      return if width is @lastWidth
      @lastWidth = width
      return if @resizePending
      @resizePending = true
      requestAnimationFrame =>
        @resizePending = false
        @draw()
    @resizeObserver.observe @root
    @update()

  localShape: (phase, ferocity) ->
    boundedFerocity = clamp ferocity, 0, 8
    amount = boundedFerocity / 8

    dwell = amount * 0.98
    halfDwell = dwell / 2
    plateau = if phase < halfDwell
      1
    else if phase > 1 - halfDwell
      -1
    else
      Math.cos PI * (phase - halfDwell) / (1 - dwell)

    point = 0.98 * (2 * amount - amount * amount)
    pointed = if point < 0.0001
      Math.cos PI * phase
    else
      Math.asin(point * Math.cos(PI * phase)) / Math.asin(point)

    shapeMix = clamp @state.mix / 100, 0, 1
    plateau + (pointed - plateau) * shapeMix

  boundary: ->
    downWeight = Math.max 0.01, 8 - clamp(@state.down, 0, 8)
    upWeight = Math.max 0.01, 8 - clamp(@state.up, 0, 8)
    TAU * downWeight / (downWeight + upWeight)

  evaluate: (theta) ->
    theta = theta % TAU
    theta += TAU if theta < 0
    boundary = @boundary()
    downstroke = theta < boundary
    if downstroke
      localPhase = theta / boundary
      position = @localShape localPhase, @state.down
    else
      localPhase = (theta - boundary) / (TAU - boundary)
      position = -@localShape localPhase, @state.up
    {position, boundary}

  directPosition: (theta, boundary) ->
    if theta < boundary
      1 - 2 * theta / boundary
    else
      -1 + 2 * (theta - boundary) / (TAU - boundary)

  peakSlope: (downstroke) ->
    boundary = @boundary()
    start = if downstroke then 0 else boundary
    finish = if downstroke then boundary else TAU
    steps = 3000
    sampleWidth = (finish - start) / steps
    peak = 0
    for index in [1...steps]
      theta = start + sampleWidth * index
      halfStep = sampleWidth / 4
      before = @evaluate(theta - halfStep).position
      after = @evaluate(theta + halfStep).position
      peak = Math.max peak, Math.abs((after - before) / (halfStep * 2))
    peak

  pathFor: (points, xScale, yScale) ->
    points.map((point, index) ->
      command = if index is 0 then 'M' else 'L'
      "#{command}#{xScale(point.x).toFixed(2)},#{yScale(point.y).toFixed(2)}"
    ).join ''

  drawAxes: (svg, width, height, margins, xScale, yScale, xTitle) ->
    innerWidth = width - margins.left - margins.right
    innerHeight = height - margins.top - margins.bottom
    group = svgElement 'g', transform: "translate(#{margins.left},#{margins.top})"
    svg.appendChild group
    group.appendChild svgElement 'rect', class: 'waveform-frame', width: innerWidth, height: innerHeight, fill: 'none'

    xTicks = if width < 520 then [0, 25, 50, 75, 100] else [0, 20, 40, 60, 80, 100]
    for tick in xTicks
      x = xScale tick
      group.appendChild svgElement 'line', class: 'waveform-tick', x1: x, x2: x, y1: innerHeight, y2: innerHeight + 5
      appendText group, "#{tick}%", class: 'waveform-tick-label', x: x, y: innerHeight + 19, 'text-anchor': 'middle'

    for tick in [-1, 0, 1]
      y = yScale tick
      group.appendChild svgElement 'line', class: 'waveform-tick', x1: -5, x2: 0, y1: y, y2: y
      appendText group, tick.toFixed(1), class: 'waveform-tick-label', x: -10, y: y + 4, 'text-anchor': 'end'

    appendText svg, xTitle, class: 'waveform-axis-label', x: margins.left + innerWidth / 2, y: height - 5, 'text-anchor': 'middle'
    yLabel = appendText svg, 'Wing position', class: 'waveform-axis-label', x: 14, y: margins.top + innerHeight / 2, 'text-anchor': 'middle'
    yLabel.setAttribute 'transform', "rotate(-90 14 #{margins.top + innerHeight / 2})"
    group

  drawChart: (svg, chart) ->
    width = Math.max 280, Math.floor(svg.getBoundingClientRect().width or @root.clientWidth)
    height = if width < 520 then 205 else 235
    margins = top: 8, right: 14, bottom: 42, left: 58
    innerWidth = width - margins.left - margins.right
    innerHeight = height - margins.top - margins.bottom
    xScale = (value) -> innerWidth * value / 100
    yScale = (value) -> innerHeight * (1.08 - value) / 2.16

    svg.setAttribute 'viewBox', "0 0 #{width} #{height}"
    svg.removeChild svg.firstChild while svg.firstChild
    title = if chart is 'cycle' then 'Beat-cycle phase' else 'Local half-stroke phase'
    group = @drawAxes svg, width, height, margins, xScale, yScale, title
    boundary = @boundary()

    if chart is 'cycle'
      phases = (TAU * index / 1200 for index in [0..1200])
      epsilon = Math.min 0.000001, boundary / 10, (TAU - boundary) / 10
      phases.push boundary - epsilon, boundary, boundary + epsilon
      phases.sort (left, right) -> left - right
      points = ({x: phase / TAU * 100, y: @evaluate(phase).position} for phase in phases)
      direct = ({x: phase / TAU * 100, y: @directPosition(phase, boundary)} for phase in phases)
      group.appendChild svgElement 'path', class: 'waveform-line direct', d: @pathFor(direct, xScale, yScale)
      group.appendChild svgElement 'path', class: 'waveform-line current', d: @pathFor(points, xScale, yScale)
      boundaryX = xScale boundary / TAU * 100
      group.appendChild svgElement 'line', class: 'waveform-line boundary', x1: boundaryX, x2: boundaryX, y1: 0, y2: innerHeight
    else
      phases = (index / 600 for index in [0..600])
      down = ({x: phase * 100, y: @localShape(phase, @state.down)} for phase in phases)
      up = ({x: phase * 100, y: @localShape(phase, @state.up)} for phase in phases)
      direct = [{x: 0, y: 1}, {x: 100, y: -1}]
      group.appendChild svgElement 'path', class: 'waveform-line direct', d: @pathFor(direct, xScale, yScale)
      group.appendChild svgElement 'path', class: 'waveform-line down', d: @pathFor(down, xScale, yScale)
      group.appendChild svgElement 'path', class: 'waveform-line up', d: @pathFor(up, xScale, yScale)

  draw: ->
    @drawChart @root.querySelector("[data-chart='cycle']"), 'cycle'
    @drawChart @root.querySelector("[data-chart='halves']"), 'halves'

  update: ->
    @root.querySelector("[data-value='down']").textContent = @state.down.toFixed 1
    @root.querySelector("[data-value='up']").textContent = @state.up.toFixed 1
    @root.querySelector("[data-value='mix']").textContent = "#{Math.round @state.mix}%"
    downShare = @boundary() / TAU * 100
    upShare = 100 - downShare
    speedRatio = @peakSlope(true) / Math.max 0.000000001, @peakSlope(false)
    metric = @root.querySelector '[data-waveform-metric]'
    metric.textContent = "Time allocation: down #{downShare.toFixed 2}% · up #{upShare.toFixed 2}% · peak phase-speed ratio #{speedRatio.toFixed 1}×"
    @draw()

document.addEventListener 'DOMContentLoaded', ->
  new FerocityWaveformExplorer root for root in document.querySelectorAll '[data-ferocity-explorer]'
