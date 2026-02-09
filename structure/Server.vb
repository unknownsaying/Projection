Imports System.Net
Imports System.Net.Sockets
Imports System.Text
Imports System.Threading
Imports System.Collections.Generic
Imports Newtonsoft.Json


Public Class VirtualSpaceServer
    Private Shared clients As New Dictionary(Of String, ClientInfo)()
    Private Shared listener As TcpListener
    Private Shared isRunning As Boolean = False
    Private Shared ReadOnly lockObj As New Object()

    Public Class ClientInfo
        Public Property Id As String
        Public Property Name As String
        Public Property Client As TcpClient
        Public Property Stream As NetworkStream
        Public Property IPAddress As String
        Public Property Location As Point3D
        Public Property Status As String
    End Class

    Public Class Point3D
        Public Property X As Double
        Public Property Y As Double
        Public Property Z As Double
    End Class

    Public Class SpaceMessage
        Public Property SenderId As String
        Public Property SenderName As String
        Public Property MessageType As String
        Public Property Content As String
        Public Property Location As Point3D
        Public Property Timestamp As DateTime
    End Class

    Public Shared Sub Main()
        Console.WriteLine("Starting Virtual Space Server...")
        StartServer("127.0.0.1", 8888)
    End Sub

    Public Shared Sub StartServer(ip As String, port As Integer)
        Try
            listener = New TcpListener(IPAddress.Parse(ip), port)
            listener.Start()
            isRunning = True
            Console.WriteLine($"Server started on {ip}:{port}")

            ' Start accepting connections
            Dim acceptThread As New Thread(AddressOf AcceptClients)
            acceptThread.Start()

            Console.WriteLine("Type 'exit' to stop server")
            While isRunning
                Dim input = Console.ReadLine()
                If input.ToLower() = "exit" Then
                    StopServer()
                End If
            End While
        Catch ex As Exception
            Console.WriteLine($"Server error: {ex.Message}")
        End Try
    End Sub

    Private Shared Sub AcceptClients()
        While isRunning
            Try
                Dim client = listener.AcceptTcpClient()
                Dim clientThread As New Thread(AddressOf HandleClient)
                clientThread.Start(client)
            Catch ex As Exception
                ' Server stopped
            End Try
        End While
    End Sub

    Private Shared Sub HandleClient(obj As Object)
        Dim client As TcpClient = DirectCast(obj, TcpClient)
        Dim clientId = Guid.NewGuid().ToString()
        Dim stream = client.GetStream()
        Dim buffer(1023) As Byte
        Dim clientInfo As New ClientInfo With {
            .Id = clientId,
            .Client = client,
            .Stream = stream,
            .IPAddress = client.Client.RemoteEndPoint.ToString(),
            .Location = New Point3D With {.X = 0, .Y = 0, .Z = 0},
            .Status = "online"
        }

        SyncLock lockObj
            clients.Add(clientId, clientInfo)
        End SyncLock

        Console.WriteLine($"Client connected: {clientId}")

        ' Send welcome message
        SendWelcomeMessage(clientInfo)

        Try
            While client.Connected AndAlso isRunning
                Dim bytesRead = stream.Read(buffer, 0, buffer.Length)
                If bytesRead > 0 Then
                    Dim message = Encoding.UTF8.GetString(buffer, 0, bytesRead)
                    ProcessMessage(clientId, message)
                End If
                Thread.Sleep(100)
            End While
        Catch ex As Exception
            Console.WriteLine($"Client {clientId} disconnected: {ex.Message}")
        Finally
            RemoveClient(clientId)
            client.Close()
        End Try
    End Sub

    Private Shared Sub ProcessMessage(clientId As String, jsonMessage As String)
        Try
            Dim message = JsonConvert.DeserializeObject(Of SpaceMessage)(jsonMessage)
            
            Select Case message.MessageType.ToLower()
                Case "join"
                    UpdateClientInfo(clientId, message.SenderName, message.Location)
                    BroadcastUserList()
                    
                Case "move"
                    UpdateClientLocation(clientId, message.Location)
                    BroadcastMovement(clientId, message.Location)
                    
                Case "message"
                    BroadcastMessage(message)
                    
                Case "whisper"
                    SendWhisper(message)
                    
                Case "status"
                    UpdateClientStatus(clientId, message.Content)
                    BroadcastStatusUpdate(clientId, message.Content)
            End Select
            
        Catch ex As Exception
            Console.WriteLine($"Error processing message: {ex.Message}")
        End Try
    End Sub

    Private Shared Sub SendWelcomeMessage(clientInfo As ClientInfo)
        Dim welcomeMsg As New SpaceMessage With {
            .MessageType = "welcome",
            .Content = $"Welcome to Virtual Space! Your ID: {clientInfo.Id}",
            .Timestamp = DateTime.Now
        }
        
        Dim json = JsonConvert.SerializeObject(welcomeMsg)
        SendToClient(clientInfo, json)
        
        ' Send current user list
        BroadcastUserList()
    End Sub

    Private Shared Sub BroadcastUserList()
        Dim userList As New List(Of Object)()
        
        SyncLock lockObj
            For Each client In clients.Values
                userList.Add(New With {
                    .Id = client.Id,
                    .Name = If(String.IsNullOrEmpty(client.Name), "Anonymous", client.Name),
                    .Location = client.Location,
                    .Status = client.Status
                })
            Next
        End SyncLock
        
        Dim listMessage As New SpaceMessage With {
            .MessageType = "userlist",
            .Content = JsonConvert.SerializeObject(userList),
            .Timestamp = DateTime.Now
        }
        
        Broadcast(JsonConvert.SerializeObject(listMessage))
    End Sub

    Private Shared Sub BroadcastMovement(clientId As String, location As Point3D)
        Dim moveMessage As New SpaceMessage With {
            .SenderId = clientId,
            .MessageType = "movement",
            .Location = location,
            .Timestamp = DateTime.Now
        }
        
        Broadcast(JsonConvert.SerializeObject(moveMessage), clientId)
    End Sub

    Private Shared Sub BroadcastMessage(message As SpaceMessage)
        Broadcast(JsonConvert.SerializeObject(message))
    End Sub

    Private Shared Sub SendWhisper(message As SpaceMessage)
        Dim targetId = message.Content.Split("|"c)(0)
        Dim whisperContent = message.Content.Split("|"c)(1)
        
        SyncLock lockObj
            If clients.ContainsKey(targetId) Then
                Dim whisperMsg As New SpaceMessage With {
                    .SenderId = message.SenderId,
                    .SenderName = message.SenderName,
                    .MessageType = "whisper",
                    .Content = whisperContent,
                    .Timestamp = DateTime.Now
                }
                
                SendToClient(clients(targetId), JsonConvert.SerializeObject(whisperMsg))
            End If
        End SyncLock
    End Sub

    Private Shared Sub UpdateClientInfo(clientId As String, name As String, location As Point3D)
        SyncLock lockObj
            If clients.ContainsKey(clientId) Then
                clients(clientId).Name = name
                If location IsNot Nothing Then
                    clients(clientId).Location = location
                End If
            End If
        End SyncLock
    End Sub

    Private Shared Sub UpdateClientLocation(clientId As String, location As Point3D)
        SyncLock lockObj
            If clients.ContainsKey(clientId) Then
                clients(clientId).Location = location
            End If
        End SyncLock
    End Sub

    Private Shared Sub UpdateClientStatus(clientId As String, status As String)
        SyncLock lockObj
            If clients.ContainsKey(clientId) Then
                clients(clientId).Status = status
            End If
        End SyncLock
    End Sub

    Private Shared Sub BroadcastStatusUpdate(clientId As String, status As String)
        Dim statusMsg As New SpaceMessage With {
            .SenderId = clientId,
            .MessageType = "statusupdate",
            .Content = status,
            .Timestamp = DateTime.Now
        }
        
        Broadcast(JsonConvert.SerializeObject(statusMsg), clientId)
    End Sub

    Private Shared Sub Broadcast(message As String, Optional excludeId As String = Nothing)
        SyncLock lockObj
            For Each client In clients.Values
                If client.Id <> excludeId Then
                    SendToClient(client, message)
                End If
            Next
        End SyncLock
    End Sub

    Private Shared Sub SendToClient(clientInfo As ClientInfo, message As String)
        Try
            Dim bytes = Encoding.UTF8.GetBytes(message)
            clientInfo.Stream.Write(bytes, 0, bytes.Length)
        Catch ex As Exception
            Console.WriteLine($"Error sending to client {clientInfo.Id}: {ex.Message}")
        End Try
    End Sub

    Private Shared Sub RemoveClient(clientId As String)
        SyncLock lockObj
            clients.Remove(clientId)
        End SyncLock
        BroadcastUserList()
        Console.WriteLine($"Client removed: {clientId}")
    End Sub

    Private Shared Sub StopServer()
        isRunning = False
        listener.Stop()
        
        SyncLock lockObj
            For Each client In clients.Values
                client.Client.Close()
            Next
            clients.Clear()
        End SyncLock
        
        Console.WriteLine("Server stopped")

    End Sub

End Class