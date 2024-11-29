# Telnet-Bridge
Telnet access from the internet

Of course you should not do this and it would be a good way of getting fired. So I decided to.

Anyway its only for contingency and I could not make any sense of this 'secure shell' business.

To make it safe I take these steps:
  - I don't expose port 23 in the router firewall. I use a different one and change it from time to time.
  - The router directs traffic to this this ESP32 which is only a bridge and anyway Telnet is not normally active.
  - An MQTT exchange (via a secure Hive session) is needed to active Telnet, which advises the external IP and presents a one-time passowrd. The Telnet session activates with a timeout.
  - You have to enter the forwarding port and pasword and get it right or telnet shuts down.
  - If you get it right you have a Telnet session routed to the destination ESP32 that requires activity to keep live.

And anyway its not powered up unless needed. How I do that from the cloud is another secret.
