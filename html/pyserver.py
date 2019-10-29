import SimpleHTTPServer
import SocketServer
import urllib
import urlparse
import serial
import sys
import time

PORT = 8080

class CustomHandler(SimpleHTTPServer.SimpleHTTPRequestHandler):
    def polatis_command(self, cmd):
        ser = serial.Serial('/dev/ttyUSB0', 38400, timeout=0.5)
        print "polatis cmd: " + cmd;
        ser.write(cmd + '\x0a')
#        time.sleep(60)
        response = ser.read(1024)
#        ser.close();
        print "polatis reply: " + self.path;
        return response

    def do_GET(self):
        line = self.path.split('?', 1)
        if len(line) == 2 and line[0] == '/command':
            response = ''
            args = urlparse.parse_qs(line[1])
            if 'type' in args and 'cmd' in args:
                cmd_type = args['type'][0];
                cmd = args['cmd'][0];
                if cmd_type.startswith('"') and cmd_type.endswith('"'):
                    cmd_type = cmd_type[1:-1]
                if cmd.startswith('"') and cmd.endswith('"'):
                    cmd = cmd[1:-1]

                if cmd_type == 'polatis':
                    response = self.polatis_command(cmd)
                else:
                    response = "Unknown command type received: " + self.path;
            else:
                response = "Missing type or cmd: " + self.path;
            print "received: " + self.path;
            print "response: " + response;
            if not response:
                response = '\x0a'
            self.request.sendall(response);
            return
        else:
            # normal requests
            SimpleHTTPServer.SimpleHTTPRequestHandler.do_GET(self)
            return

#Handler = SimpleHTTPServer.SimpleHTTPRequestHandler
Handler = CustomHandler

httpd = SocketServer.TCPServer(("", PORT), Handler)

print "serving at port", PORT
httpd.serve_forever()

