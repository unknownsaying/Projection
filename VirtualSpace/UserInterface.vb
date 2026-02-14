Public Class VirtualSpaceForm
    Private WithEvents spaceClient As New VirtualSpaceClient()
    Private userPositions As New Dictionary(Of String, Point3D)()
    
    Sub VirtualSpaceForm_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        ' Initialize UI
        lblStatus.Text = "Disconnected"
        btnConnect.Text = "Connect"
    End Sub
    
    Sub btnConnect_Click(sender As Object, e As EventArgs) Handles btnConnect.Click
        If Not spaceClient.IsConnectedToSpace Then
            Dim name = InputBox("Enter your name:", "Virtual Space", spaceClient.UserName)
            If String.IsNullOrEmpty(name) Then Exit Sub
            
            spaceClient.UserName = name
            If spaceClient.Connect("127.0.0.1", 8888, name) Then
                btnConnect.Text = "Disconnect"
                lblStatus.Text = "Connected as " & name
            End If
        Else
            spaceClient.Disconnect()
            btnConnect.Text = "Connect"
            lstUsers.Items.Clear()
            lblStatus.Text = "Disconnected"
        End If
    End Sub
    
    Sub btnSend_Click(sender As Object, e As EventArgs) Handles btnSend.Click
        If Not String.IsNullOrEmpty(txtMessage.Text) AndAlso spaceClient.IsConnectedToSpace Then
            spaceClient.SendChatMessage(txtMessage.Text)
            txtMessage.Clear()
        End If
    End Sub
    
    Sub txtMessage_KeyPress(sender As Object, e As KeyPressEventArgs) Handles txtMessage.KeyPress
        If e.KeyChar = ChrW(Keys.Enter) AndAlso spaceClient.IsConnectedToSpace Then
            spaceClient.SendChatMessage(txtMessage.Text)
            txtMessage.Clear()
            e.Handled = True
        End If
    End Sub
    
    Sub spaceClient_MessageReceived(sender As Object, message As String) Handles spaceClient.MessageReceived
        If InvokeRequired Then
            Invoke(Sub() spaceClient_MessageReceived(sender, message))
            Exit Sub
        End If
        
        txtChat.AppendText(message & vbCrLf)
        txtChat.ScrollToCaret()
    End Sub
    
    Sub spaceClient_UserListUpdated(sender As Object, users As List(Of Object)) Handles spaceClient.UserListUpdated
        If InvokeRequired Then
            Invoke(Sub() spaceClient_UserListUpdated(sender, users))
            Exit Sub
        End If
        
        lstUsers.Items.Clear()
        For Each user In users
            Dim userInfo = CTypeDynamic(Of Dictionary(Of String, Object))(user)
            Dim item = $"{userInfo("Name")} [{userInfo("Status")}]"
            lstUsers.Items.Add(item)
        Next
    End Sub
    
    Sub spaceClient_ConnectionStatusChanged(sender As Object, isConnected As Boolean) Handles spaceClient.ConnectionStatusChanged
        If InvokeRequired Then
            Invoke(Sub() spaceClient_ConnectionStatusChanged(sender, isConnected))
            Exit Sub
        End If
        
        btnConnect.Text = If(isConnected, "Disconnect", "Connect")
        lblStatus.Text = If(isConnected, "Connected", "Disconnected")
    End Sub
    
    ' Movement controls
    Sub btnMoveForward_Click(sender As Object, e As EventArgs) Handles btnMoveForward.Click
        spaceClient.SendMovement(0, 1, 0)
    End Sub
    
    Sub btnMoveBack_Click(sender As Object, e As EventArgs) Handles btnMoveBack.Click
        spaceClient.SendMovement(0, -1, 0)
    End Sub
    
    Sub btnMoveLeft_Click(sender As Object, e As EventArgs) Handles btnMoveLeft.Click
        spaceClient.SendMovement(-1, 0, 0)
    End Sub
    
    Sub btnMoveRight_Click(sender As Object, e As EventArgs) Handles btnMoveRight.Click
        spaceClient.SendMovement(1, 0, 0)
    End Sub
    
    Sub VirtualSpaceForm_FormClosing(sender As Object, e As FormClosingEventArgs) Handles MyBase.FormClosing
        spaceClient.Disconnect()
    End Sub
End Class

Public Class Point3D
    Public Property X As Double
    Public Property Y As Double
    Public Property Z As Double

End Class
