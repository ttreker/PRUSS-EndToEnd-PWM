using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Threading;

namespace BBBTCPClient
{
  public class tcpClient
	{
		public static int TCP_PORT = 5001;
		private bool rcvStarted = false;
    private Int32 blockSize = 0;
    private Int32 totalBytesReceived = 0;
    public static  string REMOTE_IP = "";
    public static bool done = false;
    public static string fileNamePrefix;

    public static void Main(String[] argv)
    {
      fileNamePrefix = GenerateFileName();
      REMOTE_IP = argv[0];
      TCP_PORT = int.Parse(argv[1]);
      tcpClient client = new tcpClient();
      client.clientProcess(REMOTE_IP);
      Console.WriteLine("Receive operation Complete.");
      Console.WriteLine("Data saved to file: " + fileNamePrefix.ToString());
      Console.WriteLine("Press any Key to Exit...");
      Console.ReadLine();
    }
    public void clientProcess(String serverName)
    {
			try
			{
        TcpClient tcpClient = new TcpClient(serverName, TCP_PORT);
				NetworkStream tcpStream = tcpClient.GetStream();
        while (tcpStream.CanRead && blockSize == 0)                                   // Get first block from Server
        {                                                                             // Expect 4 bytes (length in bytes of capture data expected)
          if (tcpStream.DataAvailable)
          {
            Byte[] received = new Byte[4];                        
            int nBytesReceived = tcpStream.Read(received, 0, received.Length);
            if (!rcvStarted && nBytesReceived != 0 && totalBytesReceived == 0)
            {
              rcvStarted = true;
            }
            blockSize = BitConverter.ToInt32(received, 0);
            Console.WriteLine("Connected to remote server");
            Console.WriteLine("Received blockSize info = " + blockSize.ToString() + " bytes");
          }
        }
        if (tcpStream.CanWrite && !done)                                              // Acknowledge and Send a GO 
				{
					Byte[] inputToBeSent = System.Text.Encoding.ASCII.GetBytes("GO".ToCharArray());
					tcpStream.Write(inputToBeSent, 0, inputToBeSent.Length);
					tcpStream.Flush();
				}
        while (tcpStream.CanRead && !done)                                            // Grab data until all bytes received and save to file.
				{
					if (tcpStream.DataAvailable)
					{
						Byte[] received = new Byte[65536];
						int nBytesReceived = tcpStream.Read(received, 0, received.Length);
            totalBytesReceived += nBytesReceived;
            BinarySave(fileNamePrefix, received, nBytesReceived);
            if (totalBytesReceived == blockSize)done = true;
          }		
				}
        if (tcpStream.CanWrite && done)                                               // Acknowledge full receipt by sending "DONE" and shut down
        {
          Byte[] inputToBeSent = System.Text.Encoding.ASCII.GetBytes("DONE".ToCharArray());
          tcpStream.Write(inputToBeSent, 0, inputToBeSent.Length);
          tcpStream.Flush();
        }
			}
			catch (Exception e)
			{
				Console.WriteLine("An Exception has occurred.");
				Console.WriteLine(e.ToString());
        Console.ReadLine();
			}
		}
    public static string GenerateFileName()
    {
      DateTime d = DateTime.Now;
      string path = System.Reflection.Assembly.GetExecutingAssembly().CodeBase;
      string directory = (System.IO.Path.GetDirectoryName(path)).Substring(6);
      return (directory + @"\Capture_" + d.Year.ToString() + d.Month.ToString() + d.Day.ToString() + " " + d.Hour.ToString() + d.Minute.ToString() + d.Second.ToString());
    }
    public void BinarySave(string prefix, byte[] b, int size)
    {
      string binFileName = prefix + ".bin";
      FileStream fs = new FileStream(binFileName, FileMode.Append);
      BinaryWriter bW = new BinaryWriter(fs);
      bW.Write(b, 0, size);
      bW.Close();
    }
  }
}
