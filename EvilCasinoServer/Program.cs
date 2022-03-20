using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.Net;
using System.Net.Sockets;

// <img src="http://www.evilcasino.net/cards/2_of_clubs.png">

namespace EvilCasinoServer
{
    internal class Program
    {
        static string ImagePath(string cardpath)
        {
            return "<img src=\"http://www.evilcasino.net/cards/" + cardpath + "\">";
        }

        static void WriteCardHtml(string template, string player_name, string card1, string card2, string card3, string card4, string card5)
        {
            template = template.Replace("*PLAYER_NAME_HERE*", player_name);

            if (card1 != "none")
            {
                template = template.Replace("*CARD_1*", ImagePath(card1));
            }
            else
            {
                template = template.Replace("*CARD_1*", "");
            }

            if (card2 != "none")
            {
                template = template.Replace("*CARD_2*", ImagePath(card2));
            }
            else
            {
                template = template.Replace("*CARD_2*", "");
            }

            if (card3 != "none")
            {
                template = template.Replace("*CARD_3*", ImagePath(card3));
            }
            else
            {
                template = template.Replace("*CARD_3*", "");
            }

            if (card4 != "none")
            {
                template = template.Replace("*CARD_4*", ImagePath(card4));
            }
            else
            {
                template = template.Replace("*CARD_4*", "");
            }

            if (card5 != "none")
            {
                template = template.Replace("*CARD_5*", ImagePath(card5));
            }
            else
            {
                template = template.Replace("*CARD_5*", "");
            }

            Directory.CreateDirectory("blackjack/" + player_name);

            File.WriteAllText("blackjack/" + player_name + "/index.html", template);
        }

        static void Main(string[] args)
        {
            string template = File.ReadAllText("index.html");

          // WriteCardHtml(template, "anukis", "3_of_spades.png", "4_of_spades.png", "5_of_spades.png", "6_of_spades.png", "7_of_spades.png");

            Console.WriteLine("Waiting for client...");

            TcpListener server = null;
            server = new TcpListener(IPAddress.Any, 1300);

            // Start listening for client requests.
            server.Start();

            // Buffer for reading data
            Byte[] bytes = new Byte[256];
            String data = null;

            // Perform a blocking call to accept requests.
            // You could also use server.AcceptSocket() here.
            TcpClient client = server.AcceptTcpClient();
            Console.WriteLine("Connected!");

            data = null;

            // Get a stream object for reading and writing
            NetworkStream stream = client.GetStream();
           
            int i;

            // Enter the listening loop.
            while (true)
            {
                // Loop to receive all the data sent by the client.
                while ((i = stream.Read(bytes, 0, bytes.Length)) != 0)
                {
                    // Translate data bytes to a ASCII string.
                    data = System.Text.Encoding.ASCII.GetString(bytes, 0, i);
                    Console.WriteLine("Received: {0}", data);


                    // name <card1> <card2> <card3> <card4> <card5>

                    string[] tokens = data.Split(' ');

                    string player_name = tokens[0];
                    string card1 = tokens[1];
                    string card2 = tokens[2];
                    string card3 = tokens[3];
                    string card4 = tokens[4];
                    string card5 = tokens[5];

                    WriteCardHtml(template, player_name, card1, card2, card3, card4, card5);
                }
            }
        }
    }
}
