const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Zutek PID Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    body { font-family: Arial; text-align: center; margin: 0; background: #1a1a1a; color: #fff; }
    .card { background: #2d2d2d; padding: 20px; margin: 10px; border-radius: 10px; display: inline-block; vertical-align: top; }
    h2 { color: #00bcd4; }
    input { padding: 5px; width: 60px; background: #444; color: #fff; border: 1px solid #666; }
    button { padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; font-weight: bold; color: white;}
    .btn-start { background: #4CAF50; }
    .btn-stop { background: #f44336; }
    .btn-update { background: #2196F3; }
    .status-ok { color: #4CAF50; }
    .status-err { color: #f44336; font-weight: bold; animation: blink 1s infinite; }
    @keyframes blink { 50% { opacity: 0; } }
  </style>
</head>
<body>
  <h2>Zutek Thermal Control</h2>

  <div class="card">
    <h3>Status</h3>
    <h1 id="tempDisplay">--.-- &deg;C</h1>
    <p>Setpoint: <span id="spDisplay">--</span> &deg;C</p>
    <p>Output: <span id="outDisplay">--</span> %</p>
    <p>State: <span id="stateDisplay">--</span></p>
    <p>USB Log: <span id="logDisplay">--</span></p>
    <button class="btn-start" onclick="toggleRun(true)">START TEST</button>
    <button class="btn-stop" onclick="toggleRun(false)">STOP</button>
  </div>

  <div class="card">
    <h3>Settings</h3>
    Target: <input type="number" id="setpoint" step="0.5"><br><br>
    Kp: <input type="number" id="kp" step="0.1">
    Ki: <input type="number" id="ki" step="0.01">
    Kd: <input type="number" id="kd" step="0.1"><br><br>
    <button class="btn-update" onclick="updateParams()">Update PID</button>
  </div>

  <div style="width: 90%; margin: auto;">
    <canvas id="tempChart"></canvas>
  </div>

<script>
  var ctx = document.getElementById('tempChart').getContext('2d');
  var chart = new Chart(ctx, {
      type: 'line',
      data: { labels: [], datasets: [
          { label: 'Temperature', borderColor: '#00bcd4', data: [] },
          { label: 'Setpoint', borderColor: '#f44336', borderDash: [5, 5], data: [] }
      ]},
      options: { animation: false, scales: { x: { display: false } } }
  });

  function updateParams() {
    var sp = document.getElementById('setpoint').value;
    var p = document.getElementById('kp').value;
    var i = document.getElementById('ki').value;
    var d = document.getElementById('kd').value;
    // Add timestamp to prevent caching
    fetch(`/api/set?sp=${sp}&kp=${p}&ki=${i}&kd=${d}&t=${Date.now()}`);
  }

  function toggleRun(state) {
    fetch(`/api/toggle?run=${state ? 1 : 0}&t=${Date.now()}`);
  }

  function fetchData() {
    // Add timestamp to prevent caching
    fetch('/api/status?t=' + Date.now()).then(response => response.json()).then(data => {
      document.getElementById('tempDisplay').innerText = data.temp.toFixed(2) + ' C';
      document.getElementById('spDisplay').innerText = data.sp.toFixed(1);
      document.getElementById('outDisplay').innerText = ((data.out/255)*100).toFixed(0);
      document.getElementById('logDisplay').innerText = data.log ? "REC" : "IDLE";

      var stEl = document.getElementById('stateDisplay');
      if(data.err == 0) { stEl.innerText = data.run ? "RUNNING" : "IDLE"; stEl.className = "status-ok"; }
      else {
        var msg = "ERROR";
        if(data.err == 1) msg = "SENSOR FAIL";
        if(data.err == 2) msg = "OVERTEMP";
        if(data.err == 3) msg = "USB FAIL";
        stEl.innerText = msg;
        stEl.className = "status-err";
      }

      // Update Inputs ONLY if user is not currently typing in them
      if(document.activeElement.tagName != "INPUT") {
         document.getElementById('setpoint').value = data.sp;
         // Ensure these are treated as floats to avoid empty strings
         document.getElementById('kp').value = parseFloat(data.kp);
         document.getElementById('ki').value = parseFloat(data.ki);
         document.getElementById('kd').value = parseFloat(data.kd);
      }

      // Graph
      if(chart.data.labels.length > 100) { chart.data.labels.shift(); chart.data.datasets[0].data.shift(); chart.data.datasets[1].data.shift(); }
      chart.data.labels.push(data.time);
      chart.data.datasets[0].data.push(data.temp);
      chart.data.datasets[1].data.push(data.sp);
      chart.update();
    });
  }

  // Fetch immediately on load, then every 1s
  window.onload = fetchData;
  setInterval(fetchData, 1000);
</script>
</body></html>
)rawliteral";
