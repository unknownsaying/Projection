using UnityEngine;
using TMPro;
using UnityEngine.UI;

public class AvatarController : MonoBehaviour
{
    public TextMeshProUGUI usernameText;
    public Image statusIndicator;
    public GameObject chatBubble;
    public TextMeshProUGUI chatText;
    
    private string userId;
    private string username;
    private bool isLocalPlayer;
    
    public void Initialize(string id, string name, bool isLocal)
    {
        userId = id;
        username = name;
        isLocalPlayer = isLocal;
        
        usernameText.text = name;
        
        if (isLocal)
        {
            GetComponent<Renderer>().material.color = Color.green;
            gameObject.AddComponent<PlayerMovement>();
        }
        else
        {
            GetComponent<Renderer>().material.color = Color.blue;
        }
    }
    
    public void UpdatePosition(Vector3 position)
    {
        transform.position = position;
    }
    
    public void ShowMessage(string message)
    {
        chatBubble.SetActive(true);
        chatText.text = message;
        
        CancelInvoke("HideMessage");
        Invoke("HideMessage", 5f);
    }
    
    private void HideMessage()
    {
        chatBubble.SetActive(false);
    }
    
    public void UpdateStatus(string status)
    {
        Color statusColor = Color.gray;
        
        switch (status.ToLower())
        {
            case "online":
                statusColor = Color.green;
                break;
            case "away":
                statusColor = Color.yellow;
                break;
            case "busy":
                statusColor = Color.red;
                break;
            case "offline":
                statusColor = Color.gray;
                break;
        }
        
        statusIndicator.color = statusColor;
    }
}