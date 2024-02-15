const socket = new WebSocket ("ws://127.0.0.1:8000");
let username;

function showChat () 
{
    const loginSection = document.getElementById ("loginSection");
    const chatSection = document.getElementById ("chatSection");

    loginSection.style.display = "none";
    chatSection.style.display = "block";
}

function setUsername () 
{
    const usernameInput = document.getElementById ("usernameInput");
    username = usernameInput.value;
    socket.send (username);

    const welcomeMessage = document.getElementById ("welcomeMessage");
    welcomeMessage.innerHTML = "Welcome " + username;

    usernameInput.value = "";
    showChat ();
}

socket.addEventListener ("message", (event) => {
    appendMessage (event.data);
});

function sendMessage () 
{
    const messageInput = document.getElementById ("messageInput");
    const message = messageInput.value;
    
    // Send the message to the server along with the username
    socket.send (message);
    messageInput.value = "";
}

function getActiveUsers () 
{
    socket.send ("activeUsers");
}

function updateName () 
{
    const messageInput = document.getElementById ("messageInput");
    const message = messageInput.value;

    if (message.length === 0) 
    {
        appendMessage ("Invalid name");
        messageInput.value = "";
        return;
    }

    socket.send ("new_name=" + message);
    messageInput.value = "";
    
    username = message;
    const welcomeMessage = document.getElementById ("welcomeMessage");
    welcomeMessage.innerHTML = "Welcome " + username;
}

function appendMessage (message) 
{
    const chatArea = document.getElementById ("chatArea");
    const messageElement = document.createElement ("div");

    chatArea.style.display = "block";
    messageElement.textContent = message;
    chatArea.appendChild (messageElement);
}
