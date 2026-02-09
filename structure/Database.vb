Imports Microsoft.EntityFrameworkCore

Public Class ApplicationDbContext
    Inherits DbContext
    
    Public Sub New(options As DbContextOptions(Of ApplicationDbContext))
        MyBase.New(options)
    End Sub
    
    Public Property Users As DbSet(Of User)
    Public Property Messages As DbSet(Of Message)
    Public Property Files As DbSet(Of File)
    Public Property Rooms As DbSet(Of Room)
    
    Protected Overrides Sub OnModelCreating(modelBuilder As ModelBuilder)
        modelBuilder.Entity(Of User)() _
            .HasIndex(Function(u) u.Username) _
            .IsUnique()
        
        modelBuilder.Entity(Of User)() _
            .HasIndex(Function(u) u.Email) _
            .IsUnique()
    End Sub
End Class

Class User
    Public Property Id As String
    Public Property Username As String
    Public Property Email As String
    Public Property PasswordHash As String
    Public Property AvatarData As String
    Public Property CreatedAt As DateTime
    Public Property LastLogin As DateTime?
End Class

Class Message
    Private Property Id As Integer
    Public Property SenderId As String
    Public Property Room As String
    Public Property Content As String
    Public Property Type As String
    Private Property Timestamp As DateTime
End Class

Class File
    Public Property FileId As String
    Public Property FileName As String
    Private Property FileSize As Long
    Public Property UploaderId As String
    Public Property RoomId As String
    Public Property Description As String
    Private Property UploadTime As DateTime
    Public Property FilePath As String
End Class

Class Room
    Public Property RoomId As String
    Public Property RoomName As String
    Public Property CreatorId As String
    Default Property MaxUsers As Integer
    Public Property IsPublic As Boolean
    Default Property CreatedAt As DateTime
        
End Class