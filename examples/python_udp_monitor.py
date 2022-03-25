import sys
import socket
import struct
import argparse

def getOptions():
    parser = argparse.ArgumentParser(description="Parses command.")
    parser.add_argument("-i", "--ip", required=True, help="IP address to bind TO")
    parser.add_argument("-p", "--port", required=True, help="IP port to bind TO")
    parser.add_argument("-v", "--version", required=False, default=1, help="CSP Address version (1 or 2)")
    return parser.parse_args(sys.argv[1:])

class CSP(object):
    def __init__(self, csp_packet, version):
        if version == 1:
            if len(csp_packet) < 4:
                raise ValueError('Malformed CSP packet (too short)')
            csp = struct.unpack('>I', csp_packet[0:4])[0]
            self.priority = (csp >> 30) & 0x3
            self.source = (csp >> 25) & 0x1f
            self.destination = (csp >> 20) & 0x1f
            self.dest_port = (csp >> 14) & 0x3f
            self.source_port = (csp >> 8) & 0x3f
            self.flags = (csp) &  0x0f

    def __str__(self):
        return ("""v1: Pri: {} Src: {} Dst: {} Dport: {} Sport: {} Flags: {}""".format(
            self.priority, self.source, self.destination, self.dest_port,
            self.source_port, self.flags))

if __name__ == "__main__":

    options = getOptions()

    # Socket to talk to server
    s = socket.socket(family=socket.AF_INET, type=socket.SOCK_DGRAM)

    # Bind to address and ip
    sinaddr = socket.gethostbyname(options.ip)
    s.bind((sinaddr, int(options.port)))

    # Process 5 updates
    total_value = 0
    for update_nbr in range (500):
        udppacket = s.recvfrom(1024)
        csp = CSP(udppacket[0][:4],1)
        print (csp)

