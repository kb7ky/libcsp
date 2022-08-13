import sys
import zmq
import struct
import argparse

def getOptions():
    parser = argparse.ArgumentParser(description="Parses command.")
    parser.add_argument("-b", "--zmq_broker_host", required=True, help="ZMQ broker hostname")
    parser.add_argument("-s", "--subPort", required=True, help="ZMQ subscriber port (7000)")
    parser.add_argument("-p", "--pubPort", required=True, help="ZMQ published port (6000)")
    return parser.parse_args(sys.argv[1:])

if __name__ == "__main__":

    options = getOptions()

    # Socket to talk to server
    context = zmq.Context()
    subSocket = context.socket(zmq.SUB)

    print ("Sub connect to tcp://%s:%s" % (options.zmq_broker_host, options.subPort))
    subSocket.connect ("tcp://%s:%s" % (options.zmq_broker_host, options.subPort))

    subSocket.setsockopt(zmq.SUBSCRIBE, b'')

    pubSocket = context.socket(zmq.PUB)
    print ("Pub connect to tcp://%s:%s" % (options.zmq_broker_host, options.pubPort))
    pubSocket.connect("tcp://%s:%s" % (options.zmq_broker_host, options.pubPort))

    topic = "ERROR"
    topicBytes = str.encode(topic)
    data = "1234 ABCDEF"
    dataBytes = str.encode(data)
    pubSocket.send(topicBytes + dataBytes)

