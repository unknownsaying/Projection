Imports System.Net.Sockets
Imports System.Text
Imports System.Threading
Imports System.IO
Imports Newtonsoft.Json
Imports NAudio.Wave
Imports System.Collections.Generic
Imports System.Drawing
Imports System.Windows.Forms

Public Class EnhancedVirtualSpaceForm
    Private mainClient As TcpClient
    Private voiceClient As TcpClient
    Private fileClient As TcpClient
    Private udpVoiceClient As UdpClient
    Private mainStream As NetworkStream
    Private voiceStream As NetworkStream
    Private fileStream As NetworkStream
    Private receiveThread As Thread
    Private voiceReceiveThread As Thread
    Private isConnected As Boolean = False
    Private userId As String = ""
    Private userName As String = ""
    Private currentRoom As String = "lobby"
    
    ' Voice chat components
    Private waveIn As WaveInEvent
    Private waveOut As WaveOutEvent
    Private bufferedWaveProvider As BufferedWaveProvider
    Private isVoiceActive As Boolean = False
    Private voicePort As Integer = 50000
    
    ' File sharing
    Private fileUploadQueue As New List(Of String)()
    Private downloadedFiles As New Dictionary(Of String, String)()
    
    ' 3D visualization simulation
    Private userPositions As New Dictionary(Of String, PointF)()
    Private userAvatars As New Dictionary(Of String, Image)()
    Private myPosition As New PointF(400, 300)
    Private avatarSize As Integer = 50
    
    ' Events for UI updates
    Public Event MessageReceived As EventHandler(Of String)
    Public Event UserListUpdated As EventHandler(Of List(Of UserInfo))
    Public Event FileListUpdated As EventHandler(Of List(Of FileInfo))
    Public Event VoiceStatusChanged As EventHandler(Of Boolean)
    
    Private Sub EnhancedVirtualSpaceForm_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        InitializeVoiceDevices()
        LoadDefaultAvatar()
        
        ' Set up 3D visualization panel
        SetupVisualizationPanel()
    End Sub
    
    Private Sub SetupVisualizationPanel()
        pnlVisualization.Paint += AddressOf VisualizationPanel_Paint
        pnlVisualization.MouseDown += AddressOf VisualizationPanel_MouseDown
        pnlVisualization.MouseMove += AddressOf VisualizationPanel_MouseMove
        pnlVisualization.DoubleBuffered = True
    End Sub
    
    Private Sub btnConnect_Click(sender As Object, e As EventArgs) Handles btnConnect.Click
        If Not isConnected Then
            If ConnectToServer("127.0.0.1", 8888) Then
                btnConnect.Text = "Disconnect"
                btnConnect.BackColor = Color.Red
                isConnected = True
                
                ' Connect to voice server
                ConnectToVoiceServer("127.0.0.1", 8889)
                
                ' Connect to file server
                ConnectToFileServer("127.0.0.1", 8890)
            End If
        Else
            DisconnectFromServer()
            btnConnect.Text = "Connect"
            btnConnect.BackColor = Color.FromArgb(0, 123, 255)
            isConnected = False
        End If
    End Sub
    
    Private Function ConnectToServer(serverIp As String, port As Integer) As Boolean
        Try
            mainClient = New TcpClient()
            mainClient.Connect(serverIp, port)
            mainStream = mainClient.GetStream()
            
            receiveThread = New Thread(AddressOf ReceiveMessages)
            receiveThread.Start()
            
            Return True
        Catch ex As Exception
            MessageBox.Show($"Connection failed: {ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return False
        End Try
    End Function
    
    Private Sub ConnectToVoiceServer(serverIp As String, port As Integer)
        Try
            voiceClient = New TcpClient()
            voiceClient.Connect(serverIp, port)
            voiceStream = voiceClient.GetStream()
            
            ' Send authentication for voice
            Dim authData = New With {
                .userId = userId,
                .voicePort = voicePort
            }
            
            SendVoiceMessage("auth", authData)
            
            ' Start UDP voice client
            udpVoiceClient = New UdpClient(voicePort)
            
            voiceReceiveThread = New Thread(AddressOf ReceiveVoiceMessages)
            voiceReceiveThread.Start()
            
        Catch ex As Exception
            Console.WriteLine($"Voice server connection failed: {ex.Message}")
        End Try
    End Sub
    
    Private Sub ConnectToFileServer(serverIp As String, port As Integer)
        Try
            fileClient = New TcpClient()
            fileClient.Connect(serverIp, port)
            fileStream = fileClient.GetStream()
        Catch ex As Exception
            Console.WriteLine($"File server connection failed: {ex.Message}")
        End Try
    End Sub
    
    Private Sub btnRegister_Click(sender As Object, e As EventArgs) Handles btnRegister.Click
        Using registerForm As New RegistrationForm()
            If registerForm.ShowDialog() = DialogResult.OK Then
                Dim registerData = New With {
                    .username = registerForm.Username,
                    .email = registerForm.Email,
                    .password = registerForm.Password
                }
                
                SendMainMessage("register", registerData)
            End If
        End Using
    End Sub
    
    Private Sub btnLogin_Click(sender As Object, e As EventArgs) Handles btnLogin.Click
        Using loginForm As New LoginForm()
            If loginForm.ShowDialog() = DialogResult.OK Then
                Dim loginData = New With {
                    .username = loginForm.Username,
                    .password = loginForm.Password
                }
                
                SendMainMessage("login", loginData)
            End If
        End Using
    End Sub
    
    Private Sub btnSendMessage_Click(sender As Object, e As EventArgs) Handles btnSendMessage.Click
        If Not String.IsNullOrEmpty(txtMessage.Text) AndAlso isConnected Then
            Dim messageData = New With {
                .content = txtMessage.Text,
                .room = currentRoom,
                .timestamp = DateTime.Now
            }
            
            SendMainMessage("chat", messageData)
            txtMessage.Clear()
        End If
    End Sub
    
    Private Sub btnToggleVoice_Click(sender As Object, e As EventArgs) Handles btnToggleVoice.Click
        If isVoiceActive Then
            StopVoiceCapture()
            btnToggleVoice.Text = "Start Voice"
            btnToggleVoice.BackColor = Color.Gray
        Else
            StartVoiceCapture()
            btnToggleVoice.Text = "Stop Voice"
            btnToggleVoice.BackColor = Color.Green
        End If
        
        isVoiceActive = Not isVoiceActive
        RaiseEvent VoiceStatusChanged(Me, isVoiceActive)
    End Sub
    
    Private Sub btnUploadFile_Click(sender As Object, e As EventArgs) Handles btnUploadFile.Click
        Using openFileDialog As New OpenFileDialog()
            openFileDialog.Filter = "All Files (*.*)|*.*"
            openFileDialog.Multiselect = True
            
            If openFileDialog.ShowDialog() = DialogResult.OK Then
                For Each fileName In openFileDialog.FileNames
                    UploadFile(fileName)
                Next
            End If
        End Using
    End Sub
    
    Private Sub UploadFile(filePath As String)
        Try
            Dim fileInfo As New FileInfo(filePath)
            Dim fileBytes = File.ReadAllBytes(filePath)
            
            ' Prepare metadata
            Dim metadata = New Dictionary(Of String, String) From {
                {"fileName", Path.GetFileName(filePath)},
                {"fileSize", fileBytes.Length.ToString()},
                {"uploaderId", userId},
                {"roomId", currentRoom},
                {"description", txtFileDescription.Text}
            }
            
            ' Send metadata
            Dim metaJson = JsonConvert.SerializeObject(metadata)
            Dim metaBytes = Encoding.UTF8.GetBytes(metaJson)
            
            Dim metaLengthBytes = BitConverter.GetBytes(metaBytes.Length)
            fileStream.Write(metaLengthBytes, 0, metaLengthBytes.Length)
            fileStream.Write(metaBytes, 0, metaBytes.Length)
            
            ' Send file data
            fileStream.Write(fileBytes, 0, fileBytes.Length)
            fileStream.Flush()
            
            Invoke(Sub()
                       lstFiles.Items.Add($"{Path.GetFileName(filePath)} - Uploading...")
                   End Sub)
            
        Catch ex As Exception
            MessageBox.Show($"Upload failed: {ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub
    
    Private Sub InitializeVoiceDevices()
        Try
            ' Initialize wave format: 16kHz, 16-bit, mono
            Dim waveFormat = New WaveFormat(16000, 16, 1)
            
            ' Setup playback
            bufferedWaveProvider = New BufferedWaveProvider(waveFormat)
            waveOut = New WaveOutEvent()
            waveOut.Init(bufferedWaveProvider)
            waveOut.Play()
            
        Catch ex As Exception
            Console.WriteLine($"Voice initialization error: {ex.Message}")
        End Try
    End Sub
    
    Private Sub StartVoiceCapture()
        Try
            waveIn = New WaveInEvent()
            waveIn.WaveFormat = New WaveFormat(16000, 16, 1)
            AddHandler waveIn.DataAvailable, AddressOf OnVoiceDataAvailable
            AddHandler waveIn.RecordingStopped, AddressOf OnVoiceRecordingStopped
            
            waveIn.StartRecording()
        Catch ex As Exception
            MessageBox.Show($"Voice capture error: {ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub
    
    Private Sub StopVoiceCapture()
        If waveIn IsNot Nothing Then
            waveIn.StopRecording()
        End If
    End Sub
    
    Private Sub OnVoiceDataAvailable(sender As Object, e As WaveInEventArgs)
        If udpVoiceClient IsNot Nothing AndAlso voicePort > 0 Then
            Try
                ' Send voice data via UDP
                udpVoiceClient.Send(e.Buffer, e.BytesRecorded, "127.0.0.1", 8891)
            Catch ex As Exception
                ' Handle UDP errors
            End Try
        End If
    End Sub
    
    Private Sub OnVoiceRecordingStopped(sender As Object, e As StoppedEventArgs)
        If waveIn IsNot Nothing Then
            waveIn.Dispose()
            waveIn = Nothing
        End If
    End Sub
    
    Private Sub ReceiveVoiceMessages()
        Try
            Dim remoteEP As New IPEndPoint(IPAddress.Any, 0)
            
            While isConnected AndAlso udpVoiceClient IsNot Nothing
                Dim receivedBytes = udpVoiceClient.Receive(remoteEP)
                If bufferedWaveProvider IsNot Nothing Then
                    bufferedWaveProvider.AddSamples(receivedBytes, 0, receivedBytes.Length)
                End If
            End While
        Catch ex As Exception
            ' UDP socket closed
        End Try
    End Sub
    
    Private Sub ReceiveMessages()
        Dim reader As New BinaryReader(mainStream, Encoding.UTF8)
        
        While isConnected AndAlso mainClient.Connected
            Try
                Dim lengthBytes = reader.ReadBytes(4)
                If lengthBytes.Length < 4 Then Exit While
                
                Dim messageLength = BitConverter.ToInt32(lengthBytes, 0)
                Dim messageBytes = reader.ReadBytes(messageLength)
                Dim json = Encoding.UTF8.GetString(messageBytes)
                
                ProcessReceivedMessage(json)
                
            Catch ex As Exception
                If isConnected Then
                    Invoke(Sub()
                               MessageBox.Show("Disconnected from server", "Info", MessageBoxButtons.OK, MessageBoxIcon.Information)
                           End Sub)
                End If
                Exit While
            End Try
        End While
    End Sub
    
    Private Sub ProcessReceivedMessage(json As String)
        Dim message = JsonConvert.DeserializeObject(Of NetworkMessage)(json)
        
        Select Case message.Type.ToLower()
            Case "login_response"
                HandleLoginResponse(message.Data)
                
            Case "register_response"
                HandleRegisterResponse(message.Data)
                
            Case "chat"
                HandleChatMessage(message.Data)
                
            Case "user_joined"
                HandleUserJoined(message.Data)
                
            Case "user_left"
                HandleUserLeft(message.Data)
                
            Case "userlist"
                HandleUserList(message.Data)
                
            Case "movement"
                HandleMovement(message.Data)
                
            Case "file_uploaded"
                HandleFileUploaded(message.Data)
                
            Case "filelist"
                HandleFileList(message.Data)
                
            Case "voice_ready"
                HandleVoiceReady(message.Data)
        End Select
    End Sub
    
    Private Sub HandleLoginResponse(data As Object)
        Dim response = JsonConvert.DeserializeObject(Of Dictionary(Of String, Object))(data.ToString())
        
        If response("status").ToString() = "success" Then
            userId = response("userId").ToString()
            userName = response("username").ToString()
            
            Invoke(Sub()
                       lblStatus.Text = $"Connected as {userName}"
                       btnLogin.Enabled = False
                       btnRegister.Enabled = False
                       btnSendMessage.Enabled = True
                       btnToggleVoice.Enabled = True
                       btnUploadFile.Enabled = True
                       
                       ' Add myself to user list
                       userPositions(userId) = myPosition
                       userAvatars(userId) = My.Resources.DefaultAvatar
                       
                       ' Request initial data
                       SendMainMessage("request_users", Nothing)
                       SendMainMessage("request_files", Nothing)
                       
                       ' Update visualization
                       pnlVisualization.Invalidate()
                   End Sub)
        Else
            Invoke(Sub()
                       MessageBox.Show(response("message").ToString(), "Login Failed", MessageBoxButtons.OK, MessageBoxIcon.Error)
                   End Sub)
        End If
    End Sub
    
    Private Sub HandleChatMessage(data As Object)
        Dim msgData = JsonConvert.DeserializeObject(Of Dictionary(Of String, String))(data.ToString())
        
        Invoke(Sub()
                   Dim sender = If(msgData.ContainsKey("senderName"), msgData("senderName"), "Unknown")
                   Dim content = msgData("content")
                   Dim timestamp = If(msgData.ContainsKey("timestamp"), msgData("timestamp"), DateTime.Now.ToString())
                   
                   txtChat.AppendText($"[{timestamp}] {sender}: {content}" & vbCrLf)
                   txtChat.ScrollToCaret()
               End Sub)
    End Sub
    
    Private Sub HandleUserJoined(data As Object)
        Dim userData = JsonConvert.DeserializeObject(Of Dictionary(Of String, String))(data.ToString())
        Dim newUserId = userData("userId")
        Dim username = userData("username")
        
        Invoke(Sub()
                   ' Add to user list
                   lstUsers.Items.Add($"{username} ({newUserId})")
                   
                   ' Add to visualization
                   Dim rnd As New Random()
                   userPositions(newUserId) = New PointF(rnd.Next(50, pnlVisualization.Width - 50), 
                                                        rnd.Next(50, pnlVisualization.Height - 50))
                   userAvatars(newUserId) = My.Resources.DefaultAvatar
                   
                   pnlVisualization.Invalidate()
               End Sub)
    End Sub
    
    Private Sub HandleMovement(data As Object)
        Dim moveData = JsonConvert.DeserializeObject(Of Dictionary(Of String, Object))(data.ToString())
        Dim moverId = moveData("userId").ToString()
        Dim position = JsonConvert.DeserializeObject(Of Dictionary(Of String, Single))(moveData("position").ToString())
        
        Invoke(Sub()
                   If userPositions.ContainsKey(moverId) Then
                       userPositions(moverId) = New PointF(position("x"), position("y"))
                       pnlVisualization.Invalidate()
                   End If
               End Sub)
    End Sub
    
    Private Sub VisualizationPanel_Paint(sender As Object, e As PaintEventArgs)
        Dim g = e.Graphics
        g.SmoothingMode = Drawing2D.SmoothingMode.AntiAlias
        
        ' Draw grid background
        For x = 0 To pnlVisualization.Width Step 20
            g.DrawLine(Pens.LightGray, x, 0, x, pnlVisualization.Height)
        Next
        
        For y = 0 To pnlVisualization.Height Step 20
            g.DrawLine(Pens.LightGray, 0, y, pnlVisualization.Width, y)
        Next
        
        ' Draw other users
        For Each kvp In userPositions
            If kvp.Key <> userId Then
                If userAvatars.ContainsKey(kvp.Key) Then
                    g.DrawImage(userAvatars(kvp.Key), kvp.Value.X - avatarSize \ 2, kvp.Value.Y - avatarSize \ 2, avatarSize, avatarSize)
                Else
                    g.FillEllipse(Brushes.Blue, kvp.Value.X - avatarSize \ 2, kvp.Value.Y - avatarSize \ 2, avatarSize, avatarSize)
                End If
                
                ' Draw name
                Dim username = kvp.Key
                For Each item In lstUsers.Items
                    If item.ToString().Contains(kvp.Key) Then
                        username = item.ToString().Split("("c)(0).Trim()
                        Exit For
                    End If
                Next
                
                g.DrawString(username, Me.Font, Brushes.Black, kvp.Value.X - avatarSize \ 2, kvp.Value.Y + avatarSize \ 2)
            End If
        Next
        
        ' Draw myself (different color)
        g.FillEllipse(Brushes.Green, myPosition.X - avatarSize \ 2, myPosition.Y - avatarSize \ 2, avatarSize, avatarSize)
        g.DrawString(userName, Me.Font, Brushes.Black, myPosition.X - avatarSize \ 2, myPosition.Y + avatarSize \ 2)
    End Sub
    
    Private Sub VisualizationPanel_MouseDown(sender As Object, e As MouseEventArgs)
        If e.Button = MouseButtons.Left Then
            myPosition = e.Location
            
            ' Send movement to server
            Dim moveData = New With {
                .position = New With {
                    .x = myPosition.X,
                    .y = myPosition.Y,
                    .z = 0
                },
                .room = currentRoom
            }
            
            SendMainMessage("movement", moveData)
            pnlVisualization.Invalidate()
        End If
    End Sub
    
    Private Sub VisualizationPanel_MouseMove(sender As Object, e As MouseEventArgs)
        If e.Button = MouseButtons.Left Then
            VisualizationPanel_MouseDown(sender, e)
        End If
    End Sub
    
    Private Sub SendMainMessage(messageType As String, data As Object)
        If mainStream Is Nothing OrElse Not mainClient.Connected Then Return
        
        Try
            Dim message = New NetworkMessage With {
                .Type = messageType,
                .SenderId = userId,
                .Data = data,
                .Timestamp = DateTime.Now
            }
            
            Dim json = JsonConvert.SerializeObject(message)
            Dim bytes = Encoding.UTF8.GetBytes(json)
            
            Dim lengthBytes = BitConverter.GetBytes(bytes.Length)
            mainStream.Write(lengthBytes, 0, lengthBytes.Length)
            mainStream.Write(bytes, 0, bytes.Length)
            mainStream.Flush()
            
        Catch ex As Exception
            MessageBox.Show($"Send error: {ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub
    
    Private Sub SendVoiceMessage(messageType As String, data As Object)
        If voiceStream Is Nothing OrElse Not voiceClient.Connected Then Return
        
        Try
            Dim message = New With {
                .type = messageType,
                .data = data
            }
            
            Dim json = JsonConvert.SerializeObject(message)
            Dim bytes = Encoding.UTF8.GetBytes(json)
            
            Dim lengthBytes = BitConverter.GetBytes(bytes.Length)
            voiceStream.Write(lengthBytes, 0, lengthBytes.Length)
            voiceStream.Write(bytes, 0, bytes.Length)
            
        Catch ex As Exception
            Console.WriteLine($"Voice send error: {ex.Message}")
        End Try
    End Sub
    
    Private Sub DisconnectFromServer()
        isConnected = False
        
        ' Stop voice
        StopVoiceCapture()
        
        ' Close connections
        If mainClient IsNot Nothing Then mainClient.Close()
        If voiceClient IsNot Nothing Then voiceClient.Close()
        If fileClient IsNot Nothing Then fileClient.Close()
        If udpVoiceClient IsNot Nothing Then udpVoiceClient.Close()
        
        ' Dispose audio devices
        If waveOut IsNot Nothing Then
            waveOut.Stop()
            waveOut.Dispose()
        End If
        
        ' Clear UI
        Invoke(Sub()
                   lstUsers.Items.Clear()
                   lstFiles.Items.Clear()
                   txtChat.Clear()
                   lblStatus.Text = "Disconnected"
                   btnLogin.Enabled = True
                   btnRegister.Enabled = True
                   btnSendMessage.Enabled = False
                   btnToggleVoice.Enabled = False
                   btnUploadFile.Enabled = False
               End Sub)
    End Sub
    
    Private Sub EnhancedVirtualSpaceForm_FormClosing(sender As Object, e As FormClosingEventArgs) Handles MyBase.FormClosing
        DisconnectFromServer()
    End Sub
    
    Private Sub LoadDefaultAvatar()
        ' Load a default avatar image
        ' In practice, you would load from resources or files
    End Sub
End Class

Public Class RegistrationForm
    Inherits Form
    
    Public Property Username As String
    Public Property Email As String
    Public Property Password As String
    
    Private txtUsername As New TextBox()
    Private txtEmail As New TextBox()
    Private txtPassword As New TextBox()
    Private txtConfirmPassword As New TextBox()
    Private btnRegister As New Button()
    Private btnCancel As New Button()
    
    Public Sub New()
        InitializeComponents()
    End Sub
    
    Private Sub InitializeComponents()
        Me.Text = "Register"
        Me.Size = New Size(300, 250)
        Me.FormBorderStyle = FormBorderStyle.FixedDialog
        Me.StartPosition = FormStartPosition.CenterParent
        
        Dim lblTitle As New Label() With {
            .Text = "Create Account",
            .Font = New Font("Arial", 12, FontStyle.Bold),
            .Location = New Point(10, 10),
            .Size = New Size(280, 20)
        }
        
        txtUsername.Location = New Point(10, 40)
        txtUsername.Size = New Size(280, 20)
        txtUsername.PlaceholderText = "Username"
        
        txtEmail.Location = New Point(10, 70)
        txtEmail.Size = New Size(280, 20)
        txtEmail.PlaceholderText = "Email"
        
        txtPassword.Location = New Point(10, 100)
        txtPassword.Size = New Size(280, 20)
        txtPassword.PasswordChar = "*"c
        txtPassword.PlaceholderText = "Password"
        
        txtConfirmPassword.Location = New Point(10, 130)
        txtConfirmPassword.Size = New Size(280, 20)
        txtConfirmPassword.PasswordChar = "*"c
        txtConfirmPassword.PlaceholderText = "Confirm Password"
        
        btnRegister.Text = "Register"
        btnRegister.Location = New Point(10, 170)
        btnRegister.Size = New Size(135, 30)
        AddHandler btnRegister.Click, AddressOf btnRegister_Click
        
        btnCancel.Text = "Cancel"
        btnCancel.Location = New Point(155, 170)
        btnCancel.Size = New Size(135, 30)
        AddHandler btnCancel.Click, AddressOf btnCancel_Click
        
        Me.Controls.AddRange({lblTitle, txtUsername, txtEmail, txtPassword, txtConfirmPassword, btnRegister, btnCancel})
    End Sub
    
    Private Sub btnRegister_Click(sender As Object, e As EventArgs)
        If txtPassword.Text <> txtConfirmPassword.Text Then
            MessageBox.Show("Passwords do not match!", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return
        End If
        
        If String.IsNullOrEmpty(txtUsername.Text) OrElse
           String.IsNullOrEmpty(txtEmail.Text) OrElse
           String.IsNullOrEmpty(txtPassword.Text) Then
            MessageBox.Show("Please fill all fields!", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return
        End If
        
        Username = txtUsername.Text
        Email = txtEmail.Text
        Password = txtPassword.Text
        
        Me.DialogResult = DialogResult.OK
        Me.Close()
    End Sub
    
    Private Sub btnCancel_Click(sender As Object, e As EventArgs)
        Me.DialogResult = DialogResult.Cancel
        Me.Close()
    End Sub
End Class

Public Class LoginForm
    Inherits Form
    
    Public Property Username As String
    Public Property Password As String
    
    Private txtUsername As New TextBox()
    Private txtPassword As New TextBox()
    Private btnLogin As New Button()
    Private btnCancel As New Button()
    
    Public Sub New()
        InitializeComponents()
    End Sub
    
    Private Sub InitializeComponents()
        Me.Text = "Login"
        Me.Size = New Size(300, 200)
        Me.FormBorderStyle = FormBorderStyle.FixedDialog
        Me.StartPosition = FormStartPosition.CenterParent
        
        Dim lblTitle As New Label() With {
            .Text = "Login to Virtual Space",
            .Font = New Font("Arial", 12, FontStyle.Bold),
            .Location = New Point(10, 10),
            .Size = New Size(280, 20)
        }
        
        txtUsername.Location = New Point(10, 40)
        txtUsername.Size = New Size(280, 20)
        txtUsername.PlaceholderText = "Username"
        
        txtPassword.Location = New Point(10, 70)
        txtPassword.Size = New Size(280, 20)
        txtPassword.PasswordChar = "*"c
        txtPassword.PlaceholderText = "Password"
        
        btnLogin.Text = "Login"
        btnLogin.Location = New Point(10, 110)
        btnLogin.Size = New Size(135, 30)
        AddHandler btnLogin.Click, AddressOf btnLogin_Click
        
        btnCancel.Text = "Cancel"
        btnCancel.Location = New Point(155, 110)
        btnCancel.Size = New Size(135, 30)
        AddHandler btnCancel.Click, AddressOf btnCancel_Click
        
        Me.Controls.AddRange({lblTitle, txtUsername, txtPassword, btnLogin, btnCancel})
    End Sub
    
    Private Sub btnLogin_Click(sender As Object, e As EventArgs)
        If String.IsNullOrEmpty(txtUsername.Text) OrElse String.IsNullOrEmpty(txtPassword.Text) Then
            MessageBox.Show("Please enter username and password!", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return
        End If
        
        Username = txtUsername.Text
        Password = txtPassword.Text
        
        Me.DialogResult = DialogResult.OK
        Me.Close()
    End Sub
    
    Private Sub btnCancel_Click(sender As Object, e As EventArgs)
        Me.DialogResult = DialogResult.Cancel
        Me.Close()
    End Sub
End Class