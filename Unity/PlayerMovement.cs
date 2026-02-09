using UnityEngine;


public class PlayerMovement : MonoBehaviour
{
    public float moveSpeed = 5f;
    public float rotationSpeed = 180f;
    
    private UnityVirtualSpaceClient networkClient;
    private CharacterController controller;
    
    void Start()
    {
        networkClient = FindObjectOfType<UnityVirtualSpaceClient>();
        controller = GetComponent<CharacterController>();
    }
    
    void Update()
    {
        // Movement input
        float horizontal = Input.GetAxis("Horizontal");
        float vertical = Input.GetAxis("Vertical");
        
        Vector3 movement = new Vector3(horizontal, 0, vertical);
        movement = transform.TransformDirection(movement);
        movement *= moveSpeed * Time.deltaTime;
        
        controller.Move(movement);
        
        // Rotation
        float mouseX = Input.GetAxis("Mouse X");
        transform.Rotate(0, mouseX * rotationSpeed * Time.deltaTime, 0);
        
        // Send updates to server
        if (networkClient != null && (movement.magnitude > 0 || Mathf.Abs(mouseX) > 0))
        {
            networkClient.SendMovement(transform.position, transform.rotation);
        }
    }
}