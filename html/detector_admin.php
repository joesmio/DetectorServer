<?php

$cmd = $_GET["cmd"];
$app = $_GET["app"];
$channel = $_GET["channel"];
$voltage = $_GET["voltage"];

if ($cmd)
{
    $server_app_name = "fake_run";
    $server_app_name = "run_cqp_detectotron";
    $server_app_name_experimental = "run_cqp_detectotron_new_histogram";
    if ($app)
        $server_app_name = $app;
//    echo "cmd = $cmd\n";
    if ($cmd == "stop_detector_server")
    {
        $cmdline = "sudo /var/www/cgi-bin/killall -9 -v -r $server_app_name 2>&1";
        echo("$cmdline\n\n");
        $output = var_dump(shell_exec($cmdline));
        echo $output;
    }
    else if ($cmd == "restart_detector_server")
    {
        $cmdline = "whoami ; sudo /var/www/cgi-bin/killall -9 -v -r run_cqp 2>&1";
        echo("$cmdline\n");
        $output = var_dump(shell_exec($cmdline));
        echo $output;

        $cmdline = "sudo /var/www/cgi-bin/$server_app_name -rerun > /var/www/cgi-bin/cqp_server_log.txt 2>&1 &";
        echo($cmdline . "\n");
        $output = var_dump(shell_exec($cmdline));
        if ($output)
            echo $output;
        usleep(500000);
        passthru("cat /var/www/cgi-bin/cqp_server_log.txt");
    }
    else if ($cmd == "restart_detector_server_experimental")
    {
        $cmdline = "whoami ; sudo /var/www/cgi-bin/killall -9 -v -r run_cqp 2>&1";
        echo("$cmdline\n");
        $output = var_dump(shell_exec($cmdline));
        echo $output;

        $cmdline = "sudo /var/www/cgi-bin/$server_app_name_experimental -rerun > /var/www/cgi-bin/cqp_server_log.txt 2>&1 &";
        echo($cmdline . "\n");
        $output = var_dump(shell_exec($cmdline));
        if ($output)
            echo $output;
        usleep(500000);
        passthru("cat /var/www/cgi-bin/cqp_server_log.txt");
    }
    else if ($cmd == "detector_server_log")
    {
        passthru("cat /var/www/cgi-bin/cqp_server_log.txt");
    }
    else if ($cmd == "check_logfile_sizes")
    {
        passthru("df -h /var/www/html/cqp_logs/ ; echo ; du -h /var/www/html/ | grep cqp_logs");
    }
    else if ($cmd == "is_running")
    {
        passthru("ps -u root | grep run_cqp");
    }
    else if ($cmd == "unlatch")
    {
        $cmdline = "sudo /var/www/cgi-bin/unlatch.py 2>&1";
        if ($channel)
            $cmdline = "sudo /var/www/cgi-bin/unlatch.py $channel 2>&1";
        echo("$cmdline\n\n");
        $output = var_dump(shell_exec($cmdline));
        echo $output;
    }
    else if ($cmd == "bias_set")
    {
        $cmdline = "/var/www/cgi-bin/bias.py $channel -s $voltage 2>&1";
        echo("$cmdline\n\n");
        $output = var_dump(shell_exec($cmdline));
        echo $output;
    }
    else if ($cmd == "bias_get")
    {
        $cmdline = "/var/www/cgi-bin/bias.py $channel 2>&1";
        echo("$cmdline\n\n");
        $output = var_dump(shell_exec($cmdline));
        echo $output;
    }
    else
    {
        echo "unknown command: $cmd\n";
    }
    return;
}
?>
<html>
<head>
<title>Detector Server Admin Console</title>
<script type="text/javascript" src="http://code.jquery.com/jquery-1.11.1.min.js"></script>
</head>
<body>
<font face="Tahoma, Digital, Arial, Helvetica, sans-serif" size="3">

<!--
<img src="images/logo_top.png" height="100"/>
<br/>
<h2>Detect-o-tron Admin Console</h2>
<br/>
-->
<button onclick="request_restart_detector_server();" style="background-color:#dfd;");">Restart Detector Server</button>
<button onclick="request_restart_detector_server_experimental();" style="background-color:#ffc;");">Restart Detector Server (experimental new histogram)</button>
<button onclick="request_stop_detector_server();" style="background-color:#fdd;">Stop Detector Server</button>
<button onclick="run_admin('detector_server_log');" style="background-color:#ddd;">View Log</button>
<button onclick="run_admin('check_logfile_sizes');" style="background-color:#ddd;">Check Logfile Sizes</button>
<button onclick="run_admin('is_running');" style="background-color:#ddd;">Check status</button>
<br/>
<hr/>
<b>Note:</b> Unlatch buttons unlatch based on the bias box number, not the detector or counter number.
<br/>
<button onclick="run_admin('unlatch');" style="background-color:#ddd;">Unlatch All</button>
<button onclick="run_admin('unlatch&channel=1');" style="background-color:#ddd;">1</button>
<button onclick="run_admin('unlatch&channel=2');" style="background-color:#ddd;">2</button>
<button onclick="run_admin('unlatch&channel=3');" style="background-color:#ddd;">3</button>
<button onclick="run_admin('unlatch&channel=4');" style="background-color:#ddd;">4</button>
<button onclick="run_admin('unlatch&channel=5');" style="background-color:#ddd;">5</button>
<button onclick="run_admin('unlatch&channel=6');" style="background-color:#ddd;">6</button>
<button onclick="run_admin('unlatch&channel=7');" style="background-color:#ddd;">7</button>
<button onclick="run_admin('unlatch&channel=8');" style="background-color:#ddd;">8</button>
<button onclick="run_admin('unlatch&channel=9');" style="background-color:#ddd;">9</button>
<button onclick="run_admin('unlatch&channel=10');" style="background-color:#ddd;">10</button>
<button onclick="run_admin('unlatch&channel=11');" style="background-color:#ddd;">11</button>
<button onclick="run_admin('unlatch&channel=12');" style="background-color:#ddd;">12</button>
<button onclick="run_admin('unlatch&channel=13');" style="background-color:#ddd;">13</button>
<button onclick="run_admin('unlatch&channel=14');" style="background-color:#ddd;">14</button>
<button onclick="run_admin('unlatch&channel=15');" style="background-color:#ddd;">15</button>
<button onclick="run_admin('unlatch&channel=16');" style="background-color:#ddd;">16</button>
<hr/>
<button onclick="run_admin('bias_set&channel=1&voltage=1.2');" style="background-color:#ddd;">Test Set Bias 1 1.2</button>
<button onclick="run_admin('bias_get&channel=1');" style="background-color:#ddd;">Test Get Bias 1</button>
<br/>
Download Win64 detector server exe <a href="win_exe" target="__blank">here</a>
<!--
<button onclick="connect_websocket();">Connect</button>
<button onclick="disconnect_websocket();">Disonnect</button>
<span id="connection_status_span">
    <font color="#a00" size="1">NOT CONNECTED TO RELAY</font>
</span>
-->
<br/>

<textarea id="output_textarea" rows="20" cols="80">
    
</textarea>
    <script type="text/javascript">

var host_addr = 'det.phy.bris.ac.uk';
var host_port = '8080';
var websocket = null;

var output_textarea = document.getElementById("output_textarea");

//var counts_text_span = document.getElementById('counts_text_span');
var counts_text_spans = [];
var active_channels = [];
for (var i = 0; i < 16; ++i)
{
    var span = document.getElementById('counts_text_span' + i);
    if (span)
    {
        counts_text_spans[i] = span;
        active_channels[i] = false;
    }
}
var coincidence_text_span = document.getElementById('coincidence_text_span');

function connect_websocket()
{
    var wsUri = 'ws://' + host_addr + ':' + host_port;
    websocket = new WebSocket(wsUri);
    websocket.onopen    = function(evt) { onOpen(evt) };
    websocket.onclose   = function(evt) { onClose(evt) };
    websocket.onmessage = function(evt) { onMessage(evt) };
    websocket.onerror   = function(evt) { onError(evt) };
    websocket.binaryType = "arraybuffer";
}

function run_admin(cmd)
{
    var url = './detector_admin.php?cmd=' + cmd + '&rand=' + Math.random();
//    output_textarea.value += url + '\n';
    $.get( url, function( data ) {
    console.log(url);
    console.log(data);
    if (output_textarea)
        output_textarea.value = data + '\n';
//        output_textarea.scrollTop(output_textarea.scrollHeight);
        $('#output_textarea').scrollTop($('#output_textarea')[0].scrollHeight);
    });
}

function request_restart_detector_server()
{
    if (confirm('Are you absolutely positive you want to restart the detector server? Please check to see that no one else is using it, as this will disrupt their work.'))
        run_admin('restart_detector_server');
}

function request_restart_detector_server_experimental()
{
    if (confirm('Are you absolutely positive you want to restart the detector server using the new histogram algorithm? Thi isn\'t well tested.'))
        run_admin('restart_detector_server_experimental');
}

function request_stop_detector_server()
{
    if (confirm('Are you absolutely positive you want to stop the detector server? Please check to see that no one else is using it, as this will disrupt their work.'))
        run_admin('stop_detector_server');
}

function disconnect_websocket()
{
    if (websocket)
    {
        onClose();
        websocket.close();
        websocket = null;
    }
}

function onOpen(evt)
{
    // Indicate connected
    var span = document.getElementById('connection_status_span');
    if (span)
        span.innerHTML = '<font color="#0a0" size="1">CONNECTED TO RELAY</font>';
}

function onClose(evt)
{
    // Indicate disconnected
    var span = document.getElementById('connection_status_span');
    if (span)
        span.innerHTML = '<font color="#a00" size="1">NOT CONNECTED TO RELAY</font>';
}

function onMessage(evt)
{
    if (typeof evt.data == 'string')
        handle_string(evt.data);
    else
        handle_binary(evt.data);
}

function onError(evt)
{
}



function handle_string(str)
{
    console.log('Got ascii data: ' + str);
}

function handle_binary(data)
{
    console.log('Got binary data');
}

function process_count_data(str)
{

}



    </script>

</font>
</body>
</html>



