Imports System.Net
Imports System.Net.Sockets
Imports System.Text
Imports System.Threading
Imports System.Collections.Generic
Imports System.IO
Imports System.Data.SqlClient
Imports Newtonsoft.Json
Imports System.Security.Cryptography
Imports NAudio.Wave
Imports System.Linq

Public Class EnhancedVirtualSpaceServer
    Private Shared clients As New Dictionary(Of String, ClientInfo)()
    Private Shared voiceClients As New Dictionary(Of String, VoiceClientInfo)()
    Private Shared files As New Dictionary(Of String, FileInfo)()
    Private Shared listener As TcpListener
    Private Shared voiceListener As TcpListener
    Private Shared fileListener As TcpListener
    Private Shared isRunning As Boolean = False
    Private Shared ReadOnly lockObj As New Object()
    Private Shared ReadOnly voiceLock As New Object()
    
    ' Database connection
    Private Shared connectionString As String = "Server=localhost;Database=VirtualSpaceDB;Integrated Security=True;"
    
    Public Class ClientInfo
        Public Property Id As String
        Public Property Name As String
        Public Property Email As String
        Public Property Client As TcpClient
        Public Property Stream As NetworkStream
        Public Property IPAddress As String
        Public Property Position As Vector3
        Public Property Rotation As Quaternion
        Public Property Status As String
        Public Property Room As String
        Public Property AvatarData As String
        Public Property LastSeen As DateTime
    End Class
    
    Public Class VoiceClientInfo
        Public Property ClientId As String
        Public Property UdpClient As UdpClient
        Public Property EndPoint As IPEndPoint
        Public Property VoiceEnabled As Boolean
    End Class
    
    Public Class FileInfo
        Public Property FileId As String
        Public Property FileName As String
        Public Property FileSize As Long
        Public Property UploaderId As String
        Public Property UploadTime As DateTime
        Public Property RoomId As String
        Public Property FilePath As String
        Public Property Description As String
    End Class
    
    Public Class Vector3
        Public Property X As Single
        Public Property Y As Single
        Public Property Z As Single
        
        Public Shared Function Distance(a As Vector3, b As Vector3) As Single
            Return CSng(Math.Sqrt((a.X - b.X) ^ 2 + (a.Y - b.Y) ^ 2 + (a.Z - b.Z) ^ 2))
        End Function
    End Class
    
    Public Class Quaternion
        Public Property X As Single
        Public Property Y As Single
        Public Property Z As Single
        Public Property W As Single
    End Class
    
    Public Class NetworkMessage
        Public Property Type As String
        Public Property SenderId As String
        Public Property Data As Object
        Public Property Timestamp As DateTime
    End Class
    
    Public Shared Sub Main()
        Console.Title = "Enhanced Virtual Space Server"
        
        ' Initialize database
        InitializeDatabase()
        
        ' Start servers on different ports
        Dim mainThread As New Thread(Sub() StartMainServer("0.0.0.0", 8888))
        Dim voiceThread As New Thread(Sub() StartVoiceServer("0.0.0.0", 8889))
        Dim fileThread As New Thread(Sub() StartFileServer("0.0.0.0", 8890))
        
        mainThread.Start()
        voiceThread.Start()
        fileThread.Start()
        
        Console.WriteLine("All servers started. Press 'exit' to stop.")
        
        While True
            Dim input = Console.ReadLine()
            If input.ToLower() = "exit" Then
                StopAllServers()
                Exit While
            End If
        End While
    End Sub
    
    Private Shared Sub InitializeDatabase()
        Try
            Using connection As New SqlConnection(connectionString)
                connection.Open()
                
                ' Create Users table
                Dim createUsersTable As String = "
                IF NOT EXISTS (SELECT * FROM sysobjects WHERE name='Users' AND xtype='U')
                CREATE TABLE Users (
                    Id NVARCHAR(50) PRIMARY KEY,
                    Username NVARCHAR(50) UNIQUE NOT NULL,
                    Email NVARCHAR(100) UNIQUE NOT NULL,
                    PasswordHash NVARCHAR(256) NOT NULL,
                    Salt NVARCHAR(128) NOT NULL,
                    AvatarData NVARCHAR(MAX),
                    CreatedAt DATETIME DEFAULT GETDATE(),
                    LastLogin DATETIME
                )"
                
                ' Create Messages table
                Dim createMessagesTable As String = "
                IF NOT EXISTS (SELECT * FROM sysobjects WHERE name='Messages' AND xtype='U')
                CREATE TABLE Messages (
                    Id INT IDENTITY(1,1) PRIMARY KEY,
                    SenderId NVARCHAR(50) FOREIGN KEY REFERENCES Users(Id),
                    Room NVARCHAR(100),
                    Content NVARCHAR(MAX),
                    Type NVARCHAR(20),
                    Timestamp DATETIME DEFAULT GETDATE()
                )"
                
                ' Create Files table
                Dim createFilesTable As String = "
                IF NOT EXISTS (SELECT * FROM sysobjects WHERE name='Files' AND xtype='U')
                CREATE TABLE Files (
                    FileId NVARCHAR(50) PRIMARY KEY,
                    FileName NVARCHAR(255) NOT NULL,
                    FileSize BIGINT,
                    UploaderId NVARCHAR(50) FOREIGN KEY REFERENCES Users(Id),
                    RoomId NVARCHAR(100),
                    Description NVARCHAR(500),
                    UploadTime DATETIME DEFAULT GETDATE(),
                    FilePath NVARCHAR(500)
                )"
                
                ' Create Rooms table
                Dim createRoomsTable As String = "
                IF NOT EXISTS (SELECT * FROM sysobjects WHERE name='Rooms' AND xtype='U')
                CREATE TABLE Rooms (
                    RoomId NVARCHAR(50) PRIMARY KEY,
                    RoomName NVARCHAR(100) NOT NULL,
                    CreatorId NVARCHAR(50) FOREIGN KEY REFERENCES Users(Id),
                    MaxUsers INT DEFAULT 50,
                    IsPublic BIT DEFAULT 1,
                    CreatedAt DATETIME DEFAULT GETDATE()
                )"
                
                Using cmd As New SqlCommand(createUsersTable, connection)
                    cmd.ExecuteNonQuery()
                End Using
                
                Using cmd As New SqlCommand(createMessagesTable, connection)
                    cmd.ExecuteNonQuery()
                End Using
                
                Using cmd As New SqlCommand(createFilesTable, connection)
                    cmd.ExecuteNonQuery()
                End Using
                
                Using cmd As New SqlCommand(createRoomsTable, connection)
                    cmd.ExecuteNonQuery()
                End Using
                
                Console.WriteLine("Database initialized successfully.")
            End Using
        Catch ex As Exception
            Console.WriteLine($"Database initialization error: {ex.Message}")
        End Try
    End Sub
    
    Private Shared Sub StartMainServer(ip As String, port As Integer)
        Try
            listener = New TcpListener(IPAddress.Parse(ip), port)
            listener.Start()
            isRunning = True
            Console.WriteLine($"Main server started on {ip}:{port}")
            
            While isRunning
                Dim client = listener.AcceptTcpClient()
                Dim clientThread As New Thread(AddressOf HandleMainClient)
                clientThread.Start(client)
            End While
        Catch ex As Exception
            Console.WriteLine($"Main server error: {ex.Message}")
        End Try
    End Sub
    
    Private Shared Sub StartVoiceServer(ip As String, port As Integer)
        Try
            voiceListener = New TcpListener(IPAddress.Parse(ip), port)
            voiceListener.Start()
            Console.WriteLine($"Voice server started on {ip}:{port}")
            
            While isRunning
                Dim client = voiceListener.AcceptTcpClient()
                Dim voiceThread As New Thread(AddressOf HandleVoiceClient)
                voiceThread.Start(client)
            End While
        Catch ex As Exception
            Console.WriteLine($"Voice server error: {ex.Message}")
        End Try
    End Sub
    
    Private Shared Sub StartFileServer(ip As String, port As Integer)
        Try
            fileListener = New TcpListener(IPAddress.Parse(ip), port)
            fileListener.Start()
            Console.WriteLine($"File server started on {ip}:{port}")
            
            While isRunning
                Dim client = fileListener.AcceptTcpClient()
                Dim fileThread As New Thread(AddressOf HandleFileClient)
                fileThread.Start(client)
            End While
        Catch ex As Exception
            Console.WriteLine($"File server error: {ex.Message}")
        End Try
    End Sub
    
    Private Shared Sub HandleMainClient(obj As Object)
        Dim client As TcpClient = DirectCast(obj, TcpClient)
        Dim stream = client.GetStream()
        Dim reader As New BinaryReader(stream, Encoding.UTF8)
        
        Try
            While client.Connected
                Dim messageLength = reader.ReadInt32()
                Dim messageBytes = reader.ReadBytes(messageLength)
                Dim json = Encoding.UTF8.GetString(messageBytes)
                Dim msg = JsonConvert.DeserializeObject(Of NetworkMessage)(json)
                
                ProcessMainMessage(msg, client, stream)
            End While
        Catch ex As Exception
            Console.WriteLine($"Client disconnected: {ex.Message}")
        Finally
            client.Close()
        End Try
    End Sub
    
    Private Shared Sub ProcessMainMessage(msg As NetworkMessage, client As TcpClient, stream As NetworkStream)
        Select Case msg.Type.ToLower()
            Case "register"
                HandleRegistration(msg.Data.ToString(), client, stream)
                
            Case "login"
                HandleLogin(msg.Data.ToString(), client, stream)
                
            Case "join_room"
                HandleJoinRoom(msg.SenderId, msg.Data.ToString(), stream)
                
            Case "chat"
                HandleChatMessage(msg.SenderId, msg.Data.ToString())
                
            Case "movement"
                HandleMovement(msg.SenderId, msg.Data.ToString())
                
            Case "avatar_update"
                HandleAvatarUpdate(msg.SenderId, msg.Data.ToString())
                
            Case "request_users"
                SendUserList(msg.SenderId, stream)
                
            Case "request_files"
                SendFileList(msg.SenderId, stream)
                
            Case "create_room"
                HandleCreateRoom(msg.SenderId, msg.Data.ToString(), stream)
                
            Case "voice_status"
                HandleVoiceStatus(msg.SenderId, CBool(msg.Data))
        End Select
    End Sub
    
    Private Shared Sub HandleRegistration(jsonData As String, client As TcpClient, stream As NetworkStream)
        Try
            Dim data = JsonConvert.DeserializeObject(Of Dictionary(Of String, String))(jsonData)
            Dim username = data("username")
            Dim email = data("email")
            Dim password = data("password")
            
            ' Hash password
            Dim salt = GenerateSalt()
            Dim passwordHash = HashPassword(password, salt)
            
            Using connection As New SqlConnection(connectionString)
                connection.Open()
                Dim query = "INSERT INTO Users (Id, Username, Email, PasswordHash, Salt) VALUES (@id, @username, @email, @hash, @salt)"
                Using cmd As New SqlCommand(query, connection)
                    Dim userId = Guid.NewGuid().ToString()
                    cmd.Parameters.AddWithValue("@id", userId)
                    cmd.Parameters.AddWithValue("@username", username)
                    cmd.Parameters.AddWithValue("@email", email)
                    cmd.Parameters.AddWithValue("@hash", passwordHash)
                    cmd.Parameters.AddWithValue("@salt", salt)
                    
                    cmd.ExecuteNonQuery()
                    
                    ' Send success response
                    Dim response = New With {
                        .status = "success",
                        .userId = userId,
                        .message = "Registration successful"
                    }
                    
                    SendResponse(stream, "register_response", response)
                End Using
            End Using
        Catch ex As Exception
            Dim response = New With {
                .status = "error",
                .message = ex.Message
            }
            SendResponse(stream, "register_response", response)
        End Try
    End Sub
    
    Private Shared Sub HandleLogin(jsonData As String, client As TcpClient, stream As NetworkStream)
        Try
            Dim data = JsonConvert.DeserializeObject(Of Dictionary(Of String, String))(jsonData)
            Dim username = data("username")
            Dim password = data("password")
            
            Using connection As New SqlConnection(connectionString)
                connection.Open()
                Dim query = "SELECT * FROM Users WHERE Username = @username"
                Using cmd As New SqlCommand(query, connection)
                    cmd.Parameters.AddWithValue("@username", username)
                    
                    Using reader = cmd.ExecuteReader()
                        If reader.Read() Then
                            Dim storedHash = reader("PasswordHash").ToString()
                            Dim salt = reader("Salt").ToString()
                            Dim userId = reader("Id").ToString()
                            
                            If VerifyPassword(password, storedHash, salt) Then
                                ' Create client info
                                Dim clientInfo As New ClientInfo With {
                                    .Id = userId,
                                    .Name = username,
                                    .Email = reader("Email").ToString(),
                                    .Client = client,
                                    .Stream = stream,
                                    .IPAddress = client.Client.RemoteEndPoint.ToString(),
                                    .Position = New Vector3 With {.X = 0, .Y = 0, .Z = 0},
                                    .Rotation = New Quaternion With {.X = 0, .Y = 0, .Z = 0, .W = 1},
                                    .Status = "online",
                                    .Room = "lobby",
                                    .AvatarData = If(IsDBNull(reader("AvatarData")), "", reader("AvatarData").ToString()),
                                    .LastSeen = DateTime.Now
                                }
                                
                                SyncLock lockObj
                                    clients(userId) = clientInfo
                                End SyncLock
                                
                                ' Update last login
                                reader.Close()
                                Dim updateQuery = "UPDATE Users SET LastLogin = GETDATE() WHERE Id = @id"
                                Using updateCmd As New SqlCommand(updateQuery, connection)
                                    updateCmd.Parameters.AddWithValue("@id", userId)
                                    updateCmd.ExecuteNonQuery()
                                End Using
                                
                                ' Send success response with user data
                                Dim response = New With {
                                    .status = "success",
                                    .userId = userId,
                                    .username = username,
                                    .avatarData = clientInfo.AvatarData,
                                    .message = "Login successful"
                                }
                                
                                SendResponse(stream, "login_response", response)
                                
                                ' Notify others in the room
                                BroadcastToRoom("lobby", "user_joined", New With {
                                    .userId = userId,
                                    .username = username,
                                    .avatarData = clientInfo.AvatarData
                                }, userId)
                            Else
                                Dim response = New With {
                                    .status = "error",
                                    .message = "Invalid password"
                                }
                                SendResponse(stream, "login_response", response)
                            End If
                        Else
                            Dim response = New With {
                                .status = "error",
                                .message = "User not found"
                            }
                            SendResponse(stream, "login_response", response)
                        End If
                    End Using
                End Using
            End Using
        Catch ex As Exception
            Dim response = New With {
                .status = "error",
                .message = ex.Message
            }
            SendResponse(stream, "login_response", response)
        End Try
    End Sub
    
    Private Shared Sub HandleVoiceClient(obj As Object)
        Dim client As TcpClient = DirectCast(obj, TcpClient)
        Dim stream = client.GetStream()
        Dim reader As New BinaryReader(stream, Encoding.UTF8)
        
        Try
            ' First message should be authentication
            Dim authLength = reader.ReadInt32()
            Dim authBytes = reader.ReadBytes(authLength)
            Dim authData = JsonConvert.DeserializeObject(Of Dictionary(Of String, String))(Encoding.UTF8.GetString(authBytes))
            Dim userId = authData("userId")
            Dim voicePort = CInt(authData("voicePort"))
            
            ' Get client endpoint for UDP voice
            Dim clientEndPoint As New IPEndPoint(DirectCast(client.Client.RemoteEndPoint, IPEndPoint).Address, voicePort)
            
            Dim voiceInfo As New VoiceClientInfo With {
                .ClientId = userId,
                .UdpClient = New UdpClient(),
                .EndPoint = clientEndPoint,
                .VoiceEnabled = True
            }
            
            SyncLock voiceLock
                voiceClients(userId) = voiceInfo
            End SyncLock
            
            ' Send voice server info
            Dim response = New With {
                .status = "connected",
                .voiceServerPort = 8891
            }
            
            SendResponse(stream, "voice_ready", response)
            
            ' Start UDP voice relay on separate thread
            Dim udpThread As New Thread(Sub() HandleUdpVoice(userId, clientEndPoint))
            udpThread.Start()
            
        Catch ex As Exception
            Console.WriteLine($"Voice client error: {ex.Message}")
            client.Close()
        End Try
    End Sub
    
    Private Shared Sub HandleUdpVoice(userId As String, clientEndPoint As IPEndPoint)
        Dim udpClient As New UdpClient(8891)
        
        Try
            While voiceClients.ContainsKey(userId) AndAlso voiceClients(userId).VoiceEnabled
                Dim remoteEP As New IPEndPoint(IPAddress.Any, 0)
                Dim receivedBytes = udpClient.Receive(remoteEP)
                
                ' Get sender's room
                Dim senderRoom As String = ""
                SyncLock lockObj
                    If clients.ContainsKey(userId) Then
                        senderRoom = clients(userId).Room
                    End If
                End SyncLock
                
                ' Relay to others in the same room
                If Not String.IsNullOrEmpty(senderRoom) Then
                    Dim roomUserIds As New List(Of String)()
                    SyncLock lockObj
                        roomUserIds = clients.Values.Where(Function(c) c.Room = senderRoom AndAlso c.Id <> userId).Select(Function(c) c.Id).ToList()
                    End SyncLock
                    
                    For Each targetId In roomUserIds
                        If voiceClients.ContainsKey(targetId) Then
                            Try
                                Dim targetInfo = voiceClients(targetId)
                                udpClient.Send(receivedBytes, receivedBytes.Length, targetInfo.EndPoint)
                            Catch ex As Exception
                                ' Target may have disconnected
                            End Try
                        End If
                    Next
                End If
            End While
        Catch ex As Exception
            Console.WriteLine($"UDP voice error: {ex.Message}")
        Finally
            udpClient.Close()
        End Try
    End Sub
    
    Private Shared Sub HandleFileClient(obj As Object)
        Dim client As TcpClient = DirectCast(obj, TcpClient)
        Dim stream = client.GetStream()
        Dim reader As New BinaryReader(stream)
        
        Try
            ' Read file metadata
            Dim metaLength = reader.ReadInt32()
            Dim metaBytes = reader.ReadBytes(metaLength)
            Dim metadata = JsonConvert.DeserializeObject(Of Dictionary(Of String, String))(Encoding.UTF8.GetString(metaBytes))
            
            Dim fileName = metadata("fileName")
            Dim fileSize = CLng(metadata("fileSize"))
            Dim uploaderId = metadata("uploaderId")
            Dim roomId = metadata("roomId")
            Dim description = metadata("description")
            
            ' Create upload directory if it doesn't exist
            Dim uploadPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Uploads")
            If Not Directory.Exists(uploadPath) Then
                Directory.CreateDirectory(uploadPath)
            End If
            
            Dim fileId = Guid.NewGuid().ToString()
            Dim filePath = Path.Combine(uploadPath, $"{fileId}_{fileName}")
            
            ' Save file
            Using fileStream As New FileStream(filePath, FileMode.Create)
                Dim buffer(8191) As Byte
                Dim bytesRemaining = fileSize
                
                While bytesRemaining > 0
                    Dim bytesToRead = CInt(Math.Min(buffer.Length, bytesRemaining))
                    Dim bytesRead = stream.Read(buffer, 0, bytesToRead)
                    fileStream.Write(buffer, 0, bytesRead)
                    bytesRemaining -= bytesRead
                End While
            End Using
            
            ' Save to database
            Using connection As New SqlConnection(connectionString)
                connection.Open()
                Dim query = "INSERT INTO Files (FileId, FileName, FileSize, UploaderId, RoomId, Description, FilePath) VALUES (@id, @name, @size, @uploader, @room, @desc, @path)"
                Using cmd As New SqlCommand(query, connection)
                    cmd.Parameters.AddWithValue("@id", fileId)
                    cmd.Parameters.AddWithValue("@name", fileName)
                    cmd.Parameters.AddWithValue("@size", fileSize)
                    cmd.Parameters.AddWithValue("@uploader", uploaderId)
                    cmd.Parameters.AddWithValue("@room", roomId)
                    cmd.Parameters.AddWithValue("@desc", description)
                    cmd.Parameters.AddWithValue("@path", filePath)
                    
                    cmd.ExecuteNonQuery()
                End Using
            End Using
            
            ' Create file info
            Dim fileInfo As New FileInfo With {
                .FileId = fileId,
                .FileName = fileName,
                .FileSize = fileSize,
                .UploaderId = uploaderId,
                .RoomId = roomId,
                .Description = description,
                .FilePath = filePath,
                .UploadTime = DateTime.Now
            }
            
            files(fileId) = fileInfo
            
            ' Notify room about new file
            BroadcastToRoom(roomId, "file_uploaded", New With {
                .fileId = fileId,
                .fileName = fileName,
                .uploaderId = uploaderId,
                .description = description,
                .fileSize = fileSize
            })
            
            ' Send success response
            Dim response = New With {
                .status = "success",
                .fileId = fileId,
                .message = "File uploaded successfully"
            }
            
            SendResponse(stream, "file_upload_response", response)
            
        Catch ex As Exception
            Dim response = New With {
                .status = "error",
                .message = ex.Message
            }
            SendResponse(stream, "file_upload_response", response)
        Finally
            client.Close()
        End Try
    End Sub
    
    Private Shared Sub SendResponse(stream As NetworkStream, responseType As String, data As Object)
        Dim response = New NetworkMessage With {
            .Type = responseType,
            .Data = data,
            .Timestamp = DateTime.Now
        }
        
        Dim json = JsonConvert.SerializeObject(response)
        Dim bytes = Encoding.UTF8.GetBytes(json)
        
        Dim lengthBytes = BitConverter.GetBytes(bytes.Length)
        stream.Write(lengthBytes, 0, lengthBytes.Length)
        stream.Write(bytes, 0, bytes.Length)
        stream.Flush()
    End Sub
    
    Private Shared Function GenerateSalt() As String
        Dim saltBytes(31) As Byte
        Using rng As New RNGCryptoServiceProvider()
            rng.GetBytes(saltBytes)
        End Using
        Return Convert.ToBase64String(saltBytes)
    End Function
    
    Private Shared Function HashPassword(password As String, salt As String) As String
        Using sha256 As New SHA256Managed()
            Dim saltedPassword = password + salt
            Dim hashBytes = sha256.ComputeHash(Encoding.UTF8.GetBytes(saltedPassword))
            Return Convert.ToBase64String(hashBytes)
        End Using
    End Function
    
    Private Shared Function VerifyPassword(password As String, storedHash As String, salt As String) As Boolean
        Dim hash = HashPassword(password, salt)
        Return hash = storedHash
    End Function
    
    Private Shared Sub BroadcastToRoom(roomId As String, messageType As String, data As Object, Optional excludeUserId As String = Nothing)
        Dim message = New NetworkMessage With {
            .Type = messageType,
            .Data = data,
            .Timestamp = DateTime.Now
        }
        
        Dim json = JsonConvert.SerializeObject(message)
        Dim bytes = Encoding.UTF8.GetBytes(json)
        
        SyncLock lockObj
            For Each client In clients.Values
                If client.Room = roomId AndAlso client.Id <> excludeUserId Then
                    Try
                        Dim lengthBytes = BitConverter.GetBytes(bytes.Length)
                        client.Stream.Write(lengthBytes, 0, lengthBytes.Length)
                        client.Stream.Write(bytes, 0, bytes.Length)
                    Catch ex As Exception
                        ' Client may have disconnected
                    End Try
                End If
            Next
        End SyncLock
    End Sub
    
    Private Shared Sub StopAllServers()
        isRunning = False
        
        If listener IsNot Nothing Then listener.Stop()
        If voiceListener IsNot Nothing Then voiceListener.Stop()
        If fileListener IsNot Nothing Then fileListener.Stop()
        
        Console.WriteLine("All servers stopped.")
    End Sub
End Class