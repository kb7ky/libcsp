import sys
import zmq
import struct
import argparse

def getOptions():
    parser = argparse.ArgumentParser(description="Parses command.")
    parser.add_argument("-b", "--zmq_broker", required=True, help="ZMQ broker address (tcp://host:port)")
    parser.add_argument("-v", "--version", required=False, default=1, help="CSP Address version (1 or 2)")
    parser.add_argument("-t", "--topiclen", required=False, default=0, help="ZMQ topic length on front of csp packet")
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
    context = zmq.Context()
    socket = context.socket(zmq.SUB)

    print ("connect to", options.zmq_broker)
    socket.connect (options.zmq_broker)

    socket.setsockopt(zmq.SUBSCRIBE, b'')

    # Process 5 updates
    update_nbr = 0
    for update_nbr in range (500):
        string = socket.recv()
        if options.topiclen > 0:
            print ("Topic:", string[:options.topiclen])
            csp = CSP(string[options.topiclen:4 + options.topiclen],1)
        else:
            csp = CSP(string[:4],1)
        print (csp)
        update_nbr = update_nbr + 1

