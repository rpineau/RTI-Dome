// Alpaca done server
//

#ifndef RTI_DOME_ALPACA_SERVER
#define RTI_DOME_ALPACA_SERVER

#include <SPI.h>
#include <Ethernet.h>

#ifndef OK
#define OK 0
#endif

#ifndef SERVER_ERROR
#define SERVER_ERROR -1
#endif

class RTIDomeAlpacaServer
{
public :
    RTIDomeAlpacaServer(int port);
    void startServer();
    int checkForRequest();

private :
    int m_nRestPort;
    int m_nDiscoveryPort;

    EthernetServer *m_RestServer;

};

RTIDomeAlpacaServer::RTIDomeAlpacaServer(int port)
{
    m_nRestPort = port;
}


void RTIDomeAlpacaServer::startServer()
{
    m_RestServer =  new EthernetServer(m_nRestPort);
    m_RestServer->begin();
}

int RTIDomeAlpacaServer::checkForRequest()
{
    int nErr = OK;
    bool currentLineIsBlank = true;
    char c;
    String sRestBuffer;

    EthernetClient client = m_RestServer->available();
    if (client) {
        while (client.connected()) {
            if (client.available()) {
                char c = client.read();
                // if you've gotten to the end of the line (received a newline
                // character) and the line is blank, the http request has ended,
                // so you can parse the request and send the response
                if (c == '\n' && currentLineIsBlank) {
                    // parse request
                    // send response
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: application/json");
                    client.println("Connection: close");  // the connection will be closed after completion of the response
                    client.println();
                    // we're done
                    break;
                }
                if (c == '\n') {
                    // you're starting a new line
                    currentLineIsBlank = true;
                }
                else if (c != '\r') {
                    // you've gotten a character on the current line
                    currentLineIsBlank = false;
                    // add data to buffer
                    sRestBuffer+= String(c);
                }
            }
        }
        delay(1);
        client.stop();
    }
    return nErr;
}
#endif // RTI_DOME_ALPACA_SERVER
