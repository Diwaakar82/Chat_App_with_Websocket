const socket = new WebSocket ("ws://127.0.0.1:8000");
let username = undefined;

socket.addEventListener ("message", (event) => {
    appendMessage (event.data);
});

function addUsername ()
{
    const chatArea = document.getElementById ("chatArea");
    const messageElement = document.createElement ("div");

    chatArea.style.display = "block";
    messageElement.textContent = "Please enter your name";
    chatArea.appendChild (messageElement);
}

function sendMessage () 
{
    if (username === undefined)
    {
        addUsername ();
        return;
    }

    const messageInput = document.getElementById ("messageInput");
    const message = messageInput.value;
    const index = message.indexOf (":");
    let request;

    if (index !== -1)
    {
        request = '{Type: 3, User: ' + message.slice (0, index) + ', Message: ' + message.slice (index + 1) + '}';
    }
    else
    {
        request = '{Type: 2, Message: ' + message + '}';
    }

    // Send the message to the server along with the username
    console.log (request);
    socket.send (request);
    messageInput.value = "";
}

function getActiveUsers () 
{
    if (username === undefined)
    {
        addUsername ();
        return;
    }

    const request = '{Type: 4}';
    console.log (request);
    socket.send (request);
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

    const request = '{"Type": 1, "Message": "' + message + '"}';
    console.log (request)
    socket.send (request);
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

    if (username === undefined)
    {
        message = "Please enter your name";
        messageElement.textContent = message;
    }
    else if (message.includes ("Name already exists"))
    {
        username = undefined;
        const welcomeMessage = document.getElementById ("welcomeMessage");
        welcomeMessage.innerHTML = "Enter valid username";

        index = message.indexOf ("Message: ") + 9;
        messageElement.textContent = message.slice (index, message.length - 1);
    }
    else
    {
        let index;
        if (message.includes ("Users: "))
        {
            index = message.indexOf ("Users: ") + 7;
        }
        else
        {
            index = message.indexOf ("\"Message\": ") + 12;
        }
        messageElement.textContent = message.slice (index, message.length - 3);

        if (messageElement.textContent.length === 0)
        messageElement.textContent = "No active users!!!";
    }
    chatArea.appendChild (messageElement);
}
