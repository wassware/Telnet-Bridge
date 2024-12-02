# Telnet-Bridge
As some devices are on fixed ip and some dhcp its a fiddle to track the ip addresses to conect to with Telnet. And I cant get anywhere with assigned hostname on ESP32.

So as there is a DNS managed by MqttBroker this Telnet bridge provides a bridge to any device from one fixed IP address - usually x.x.x.202

It has another trick to provide Telnet access from the internet. Of course you should not do this and it would be a good way of getting fired. So I decided to.

Anyway its only for contingency and I could not make any sense of this 'secure shell' business.

Switching to internet access is via an exchange via mqtt secure connection. This changes the default Telnet port 23 to some other port exposed in the router firewall (wouldn't you like to know which port...). This exchange advises the external IP and presents a one-time passowrd. The Telnet session activates with a timeout.

The first line entered has to be the correct one time password and the next the device name to bridge to. Any errors and it silently terminates the session.

