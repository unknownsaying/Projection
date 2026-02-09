using System;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEngine;
using Newtonsoft.Json;
using System.Collections.Generic;

public class UnityVirtualSpaceClient : MonoBehaviour
{
    private TcpClient client;
    private NetworkStream stream;
    private Thread receiveThread;
    private bool isConnected = false;
    
    public string serverIP = "127.0.0.1";
    public int serverPort = 8888;
    
    public GameObject userPrefab;
    public Transform usersContainer;
    
    private Dictionary<string, GameObject> userObjects = new Dictionary<string, GameObject>();
    private string userId;
    private string username;
    
    void Start()
    {
        // Load or create user credentials
        LoadUserData();
    }
    
    public void ConnectToServer()
    {
        try
        {
            client = new TcpClient();
            client.Connect(serverIP, serverPort);
            stream = client.GetStream();
            isConnected = true;
            
            receiveThread = new Thread(ReceiveMessages);
            receiveThread.Start();
            
            // Send login/register
            SendLogin();
        }
        catch (Exception ex)
        {
            Debug.LogError($"Connection failed: {ex.Message}");
        }
    }
    
    private void SendLogin()
    {
        var loginData = new
        {
            type = "login",
            username = username,
            password = "stored_password_hash" // In practice, use secure storage
        };
        
        SendMessage(JsonConvert.SerializeObject(loginData));
    }
    
    private void SendMessage(string json)
    {
        if (!isConnected || stream == null) return;
        
        try
        {
            byte[] bytes = Encoding.UTF8.GetBytes(json);
            byte[] lengthBytes = BitConverter.GetBytes(bytes.Length);
            
            stream.Write(lengthBytes, 0, lengthBytes.Length);
            stream.Write(bytes, 0, bytes.Length);
            stream.Flush();
        }
        catch (Exception ex)
        {
            Debug.LogError($"Send error: {ex.Message}");
        }
    }
    
    private void ReceiveMessages()
    {
        byte[] buffer = new byte[1024];
        
        while (isConnected && client.Connected)
        {
            try
            {
                // Read message length
                byte[] lengthBytes = new byte[4];
                stream.Read(lengthBytes, 0, 4);
                int messageLength = BitConverter.ToInt32(lengthBytes, 0);
                
                // Read message
                byte[] messageBytes = new byte[messageLength];
                int bytesRead = 0;
                while (bytesRead < messageLength)
                {
                    bytesRead += stream.Read(messageBytes, bytesRead, messageLength - bytesRead);
                }
                
                string json = Encoding.UTF8.GetString(messageBytes);
                ProcessMessage(json);
            }
            catch (Exception ex)
            {
                Debug.LogError($"Receive error: {ex.Message}");
                break;
            }
        }
    }
    
    private void ProcessMessage(string json)
    {
        UnityMainThreadDispatcher.Instance.Enqueue(() =>
        {
            try
            {
                var message = JsonConvert.DeserializeObject<NetworkMessage>(json);
                
                switch (message.type)
                {
                    case "login_response":
                        HandleLoginResponse(message.data);
                        break;
                        
                    case "userlist":
                        HandleUserList(message.data);
                        break;
                        
                    case "movement":
                        HandleMovement(message.data);
                        break;
                        
                    case "chat":
                        HandleChat(message.data);
                        break;
                }
            }
            catch (Exception ex)
            {
                Debug.LogError($"Process message error: {ex.Message}");
            }
        });
    }
    
    private void HandleLoginResponse(object data)
    {
        var response = JsonConvert.DeserializeObject<Dictionary<string, object>>(data.ToString());
        
        if (response["status"].ToString() == "success")
        {
            userId = response["userId"].ToString();
            username = response["username"].ToString();
            
            Debug.Log($"Logged in as {username}");
            
            // Create my avatar
            CreateUserAvatar(userId, username, true);
            
            // Request user list
            SendMessage(JsonConvert.SerializeObject(new { type = "request_users" }));
        }
    }
    
    private void HandleUserList(object data)
    {
        var users = JsonConvert.DeserializeObject<List<UserData>>(data.ToString());
        
        foreach (var user in users)
        {
            if (user.id != userId)
            {
                CreateUserAvatar(user.id, user.username, false);
            }
        }
    }
    
    private void HandleMovement(object data)
    {
        var moveData = JsonConvert.DeserializeObject<MovementData>(data.ToString());
        
        if (userObjects.ContainsKey(moveData.userId))
        {
            Vector3 position = new Vector3(moveData.position.x, moveData.position.y, moveData.position.z);
            userObjects[moveData.userId].transform.position = position;
        }
    }
    
    private void CreateUserAvatar(string id, string name, bool isMe)
    {
        if (userObjects.ContainsKey(id)) return;
        
        GameObject userObj = Instantiate(userPrefab, usersContainer);
        userObj.name = $"{name}_{id}";
        
        // Customize based on user data
        var avatar = userObj.GetComponent<AvatarController>();
        if (avatar != null)
        {
            avatar.Initialize(id, name, isMe);
        }
        
        userObjects[id] = userObj;
    }
    
    public void SendMovement(Vector3 position, Quaternion rotation)
    {
        var moveData = new
        {
            type = "movement",
            position = new { x = position.x, y = position.y, z = position.z },
            rotation = new { x = rotation.x, y = rotation.y, z = rotation.z, w = rotation.w },
            room = "default"
        };
        
        SendMessage(JsonConvert.SerializeObject(moveData));
    }
    
    public void SendChat(string message)
    {
        var chatData = new
        {
            type = "chat",
            content = message,
            room = "default"
        };
        
        SendMessage(JsonConvert.SerializeObject(chatData));
    }
    
    void OnDestroy()
    {
        Disconnect();
    }
    
    private void Disconnect()
    {
        isConnected = false;
        
        if (client != null)
        {
            client.Close();
        }
        
        if (receiveThread != null && receiveThread.IsAlive)
        {
            receiveThread.Join(1000);
        }
    }
    
    private void LoadUserData()
    {
        // Load from PlayerPrefs or other storage
        username = PlayerPrefs.GetString("username", "UnityUser_" + UnityEngine.Random.Range(1000, 9999));
    }
}

[System.Serializable]
public class NetworkMessage
{
    public string type;
    public string senderId;
    public object data;
    public System.DateTime timestamp;
}

[System.Serializable]
public class UserData
{
    public string id;
    public string username;
    public string status;
    public Position position;
}

[System.Serializable]
public class Position
{
    public float x;
    public float y;
    public float z;
}

[System.Serializable]
public class MovementData
{
    public string userId;
    public Position position;
    public Rotation rotation;
}

[System.Serializable]
public class Rotation
{
    public float x;
    public float y;
    public float z;
    public float w;
}