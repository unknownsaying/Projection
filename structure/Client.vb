Imports System.Net.Sockets
Imports System.Text
Imports System.Threading
Imports Newtonsoft.Json

Public Class VirtualSpaceClient
    Private client As TcpClient
    Private stream As NetworkStream
    Private receiveThread As Thread
    Private isConnected As Boolean = False
    Private userId As String
    Private userName As String = "Guest"
    
    ' Event for UI updates
    Public Event MessageReceived As EventHandler(Of String)
    Public Event UserListUpdated As EventHandler(Of List(Of Object))
    Public Event ConnectionStatusChanged As EventHandler(Of Boolean)
    
    Public Sub New()
        userId = Guid.NewGuid().ToString()
    End Sub
    
    Public Function Connect(serverIp As String, port As Integer, name As String) As Boolean
        Try
            userName = name
            client = New TcpClient()
            client.Connect(serverIp, port)
            stream = client.GetStream()
            isConnected = True
            
            ' Start receive thread
            receiveThread = New Thread(AddressOf ReceiveMessages)
            receiveThread.Start()
            
            RaiseEvent ConnectionStatusChanged(Me, True)
            
            ' Send join message
            SendJoinMessage()
            
            Return True
            
        Catch ex As Exception
            RaiseEvent MessageReceived(Me, $"Connection failed: {ex.Message}")
            Return False
        End Try
    End Function
    
    Private Sub SendJoinMessage()
        Dim joinMessage As New With {
            .MessageType = "join",
            .SenderId = userId,
            .SenderName = userName,
            .Location = New With {.X = 0, .Y = 0, .Z = 0},
            .Timestamp = DateTime.Now
        }
        
        SendMessage(JsonConvert.SerializeObject(joinMessage))
    End Sub
    
    Public Sub SendChatMessage(message As String)
        Dim chatMessage As New With {
            .MessageType = "message",
            .SenderId = userId,
            .SenderName = userName,
            .Content = message,
            .Timestamp = DateTime.Now
        }
        
        SendMessage(JsonConvert.SerializeObject(chatMessage))
    End Sub
    
    Public Sub SendMovement(x As Double, y As Double, z As Double)
        Dim moveMessage As New With {
            .MessageType = "move",
            .SenderId = userId,
            .Location = New With {.X = x, .Y = y, .Z = z},
            .Timestamp = DateTime.Now
        }
        
        SendMessage(JsonConvert.SerializeObject(moveMessage))
    End Sub
    
    Public Sub SendWhisper(targetId As String, message As String)
        Dim whisperMessage As New With {
            .MessageType = "whisper",
            .SenderId = userId,
            .SenderName = userName,
            .Content = $"{targetId}|{message}",
            .Timestamp = DateTime.Now
        }
        
        SendMessage(JsonConvert.SerializeObject(whisperMessage))
    End Sub
    
    Public Sub UpdateStatus(status As String)
        Dim statusMessage As New With {
            .MessageType = "status",
            .SenderId = userId,
            .Content = status,
            .Timestamp = DateTime.Now
        }
        
        SendMessage(JsonConvert.SerializeObject(statusMessage))
    End Sub
    
    Private Sub SendMessage(json As String)
        If isConnected AndAlso stream IsNot Nothing Then
            Try
                Dim bytes = Encoding.UTF8.GetBytes(json)
                stream.Write(bytes, 0, bytes.Length)
            Catch ex As Exception
                RaiseEvent MessageReceived(Me, $"Send error: {ex.Message}")
                Disconnect()
            End Try
        End If
    End Sub
    
    Private Sub ReceiveMessages()
        Dim buffer(1023) As Byte
        
        While isConnected AndAlso client.Connected
            Try
                If stream.DataAvailable Then
                    Dim bytesRead = stream.Read(buffer, 0, buffer.Length)
                    If bytesRead > 0 Then
                        Dim message = Encoding.UTF8.GetString(buffer, 0, bytesRead)
                        ProcessReceivedMessage(message)
                    End If
                End If
                Thread.Sleep(100)
            Catch ex As Exception
                If isConnected Then
                    RaiseEvent MessageReceived(Me, $"Receive error: {ex.Message}")
                    Disconnect()
                End If
            End Try
        End While
    End Sub
    
    Private Sub ProcessReceivedMessage(json As String)
        Try
            Dim message = JsonConvert.DeserializeObject(Of Dictionary(Of String, Object))(json)
            Dim messageType = message("MessageType").ToString()
            
            Select Case messageType
                Case "welcome"
                    RaiseEvent MessageReceived(Me, $"System: {message("Content")}")
                    
                Case "userlist"
                    Dim users = JsonConvert.DeserializeObject(Of List(Of Object))(message("Content").ToString())
                    RaiseEvent UserListUpdated(Me, users)
                    
                Case "message"
                    Dim sender = If(message.ContainsKey("SenderName"), message("SenderName").ToString(), "Unknown")
                    Dim content = message("Content").ToString()
                    RaiseEvent MessageReceived(Me, $"{sender}: {content}")
                    
                Case "movement"
                    Dim senderId = message("SenderId").ToString()
                    Dim location = message("Location")
                    ' Process movement (update user position in your UI)
                    
                Case "whisper"
                    Dim sender = If(message.ContainsKey("SenderName"), message("SenderName").ToString(), "Unknown")
                    Dim content = message("Content").ToString()
                    RaiseEvent MessageReceived(Me, $"[Whisper from {sender}]: {content}")
                    
                Case "statusupdate"
                    Dim senderId = message("SenderId").ToString()
                    Dim status = message("Content").ToString()
                    ' Update user status in UI
                    
            End Select
            
        Catch ex As Exception
            RaiseEvent MessageReceived(Me, $"Error processing message: {ex.Message}")
        End Try
    End Sub
    
    Public Sub Disconnect()
        isConnected = False
        
        If stream IsNot Nothing Then
            stream.Close()
        End If
        
        If client IsNot Nothing Then
            client.Close()
        End If
        
        If receiveThread IsNot Nothing AndAlso receiveThread.IsAlive Then
            receiveThread.Join(1000)
        End If
        
        RaiseEvent ConnectionStatusChanged(Me, False)
        RaiseEvent MessageReceived(Me, "Disconnected from virtual space")
    End Sub
    
    Public ReadOnly Property IsConnectedToSpace As Boolean
        Get
            Return isConnected
        End Get
    End Property
    
    Public ReadOnly Property UserID As String
        Get
            Return userId
        End Get
    End Property
    
    Public Property UserName As String
        Get
            Return userName
        End Get
        Set(value As String)
            userName = value
        End Set
    End Property
    
End Class