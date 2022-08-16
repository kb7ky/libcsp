import sys
import time
import zmq
import struct
import argparse

def getOptions():
    parser = argparse.ArgumentParser(description="Parses command.")
    parser.add_argument("-b", "--zmq_broker_host", required=True, help="ZMQ broker hostname")
    parser.add_argument("-s", "--subPort", required=True, help="ZMQ subscriber port (7000)")
    parser.add_argument("-p", "--pubPort", required=True, help="ZMQ publisher port (6000)")
    return parser.parse_args(sys.argv[1:])

if __name__ == "__main__":

    options = getOptions()

    # Socket to talk to server
    context = zmq.Context()
    subSocket = context.socket(zmq.SUB)

    print ("Sub connect to tcp://%s:%s" % (options.zmq_broker_host, options.subPort))
    subSocket.connect ("tcp://%s:%s" % (options.zmq_broker_host, options.subPort))

    subSocket.setsockopt(zmq.SUBSCRIBE, b'ERROR')

    pubSocket = context.socket(zmq.PUB)
    print ("Pub connect to tcp://%s:%s" % (options.zmq_broker_host, options.pubPort))
    pubSocket.connect("tcp://%s:%s" % (options.zmq_broker_host, options.pubPort))

    topic = "ERROR"
    topicBytes = str.encode(topic)
    data = "1234 ABCDEF"
    dataBytes = str.encode(data)
    pubSocket.send(b'ERROR')  

    # Process 5 updates
    update_nbr = 0
    flags = zmq.NOBLOCK
    for update_nbr in range (50):
        pubSocket.send(b'ORDER|BRIDGE1|MQTT0|001|ENCRYPT 1 1')
        try: 
            string = subSocket.recv(flags)
            flags = 0
        except zmq.ZMQError as err:
            print("ro recv")
            string = "nothing"
        
        print("sent %d" % update_nbr)
        print("recv %s" % string)
        time.sleep(1)
        update_nbr = update_nbr + 1



    context.destroy()

