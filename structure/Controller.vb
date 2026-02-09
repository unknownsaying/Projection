Imports Microsoft.AspNetCore.Mvc
Imports Microsoft.EntityFrameworkCore
Imports System.Threading.Tasks
Imports VirtualSpaceWebAPI.Models
Imports Microsoft.AspNetCore.SignalR
Imports System.Text.Json

<ApiController>
<Route("api/[controller]")>
Public Class VirtualSpaceController
    Inherits ControllerBase
    
    Private ReadOnly _context As ApplicationDbContext
    Private ReadOnly _hubContext As IHubContext(Of VirtualSpaceHub)
    
    Public Sub New(context As ApplicationDbContext, hubContext As IHubContext(Of VirtualSpaceHub))
        _context = context
        _hubContext = hubContext
    End Sub
    
    <HttpPost("register")>
    Public Async Function Register(<FromBody> request As RegisterRequest) As Task(Of IActionResult)
        ' Check if user exists
        If Await _context.Users.AnyAsync(Function(u) u.Username = request.Username OrElse u.Email = request.Email) Then
            Return BadRequest("User already exists")
        End If
        
        ' Create user
        Dim user = New User With {
            .Id = Guid.NewGuid().ToString(),
            .Username = request.Username,
            .Email = request.Email,
            .PasswordHash = BCrypt.Net.BCrypt.HashPassword(request.Password),
            .CreatedAt = DateTime.UtcNow
        }
        
        _context.Users.Add(user)
        Await _context.SaveChangesAsync()
        
        Return Ok(New With {
            .status = "success",
            .userId = user.Id,
            .message = "Registration successful"
        })
    End Function
    
    <HttpPost("login")>
    Public Async Function Login(<FromBody> request As LoginRequest) As Task(Of IActionResult)
        Dim user = Await _context.Users.FirstOrDefaultAsync(Function(u) u.Username = request.Username)
        
        If user Is Nothing OrElse Not BCrypt.Net.BCrypt.Verify(request.Password, user.PasswordHash) Then
            Return Unauthorized("Invalid credentials")
        End If
        
        ' Update last login
        user.LastLogin = DateTime.UtcNow
        Await _context.SaveChangesAsync()
        
        Return Ok(New With {
            .status = "success",
            .userId = user.Id,
            .username = user.Username,
            .avatarData = user.AvatarData
        })
    End Function
    
    <HttpGet("rooms")>
    Public Async Function GetRooms() As Task(Of IActionResult)
        Dim rooms = Await _context.Rooms.Where(Function(r) r.IsPublic).ToListAsync()
        Return Ok(rooms)
    End Function
    
    <HttpPost("rooms")>
    Public Async Function CreateRoom(<FromBody> request As CreateRoomRequest) As Task(Of IActionResult)
        Dim room = New Room With {
            .RoomId = Guid.NewGuid().ToString(),
            .RoomName = request.RoomName,
            .CreatorId = request.CreatorId,
            .MaxUsers = request.MaxUsers,
            .IsPublic = request.IsPublic,
            .CreatedAt = DateTime.UtcNow
        }
        
        _context.Rooms.Add(room)
        Await _context.SaveChangesAsync()
        
        ' Notify via SignalR
        Await _hubContext.Clients.All.SendAsync("RoomCreated", room)
        
        Return Ok(room)
    End Function
    
    <HttpGet("files/{roomId}")>
    Public Async Function GetFiles(roomId As String) As Task(Of IActionResult)
        Dim files = Await _context.Files.Where(Function(f) f.RoomId = roomId).OrderByDescending(Function(f) f.UploadTime).ToListAsync()
        Return Ok(files)
    End Function
    
    <HttpPost("upload")>
    Public Async Function UploadFile(<FromForm> file As IFormFile, <FromForm> uploaderId As String, <FromForm> roomId As String, <FromForm> description As String) As Task(Of IActionResult)
        If file Is Nothing OrElse file.Length = 0 Then
            Return BadRequest("No file uploaded")
        End If
        
        Dim uploadPath = Path.Combine(Directory.GetCurrentDirectory(), "wwwroot", "uploads")
        If Not Directory.Exists(uploadPath) Then
            Directory.CreateDirectory(uploadPath)
        End If
        
        Dim fileName = $"{Guid.NewGuid()}_{file.FileName}"
        Dim filePath = Path.Combine(uploadPath, fileName)
        
        Using stream = New FileStream(filePath, FileMode.Create)
            Await file.CopyToAsync(stream)
        End Using
        
        Dim fileRecord = New Models.File With {
            .FileId = Guid.NewGuid().ToString(),
            .FileName = file.FileName,
            .FileSize = file.Length,
            .UploaderId = uploaderId,
            .RoomId = roomId,
            .Description = description,
            .FilePath = $"/uploads/{fileName}",
            .UploadTime = DateTime.UtcNow
        }
        
        _context.Files.Add(fileRecord)
        Await _context.SaveChangesAsync()
        
        ' Notify via SignalR
        Await _hubContext.Clients.Group(roomId).SendAsync("FileUploaded", fileRecord)
        
        Return Ok(fileRecord)
    End Function
    
    <HttpGet("users/{roomId}")>
    Public Async Function GetOnlineUsers(roomId As String) As Task(Of IActionResult)
        ' This would connect to your main server to get real-time users
        ' For now, return mock data
        Dim users = New List(Of Object) From {
            New With {.Id = "1", .Username = "User1", .Status = "online"},
            New With {.Id = "2", .Username = "User2", .Status = "online"}
        }
        
        Return Ok(users)
    End Function
End Class

Public Class VirtualSpaceHub
    Inherits Hub
    
    Public Async Function JoinRoom(roomId As String, userId As String, username As String) As Task
        Await Groups.AddToGroupAsync(Context.ConnectionId, roomId)
        Await Clients.Group(roomId).SendAsync("UserJoined", New With {.UserId = userId, .Username = username})
    End Function
    
    Public Async Function LeaveRoom(roomId As String, userId As String) As Task
        Await Groups.RemoveFromGroupAsync(Context.ConnectionId, roomId)
        Await Clients.Group(roomId).SendAsync("UserLeft", New With {.UserId = userId})
    End Function
    
    Public Async Function SendMessage(roomId As String, userId As String, username As String, message As String) As Task
        Await Clients.Group(roomId).SendAsync("ReceiveMessage", New With {
            .UserId = userId,
            .Username = username,
            .Message = message,
            .Timestamp = DateTime.UtcNow
        })
    End Function

End Class