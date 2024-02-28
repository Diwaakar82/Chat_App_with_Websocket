const socket = new WebSocket ("ws://127.0.0.1:8000");
let username = undefined;

socket.addEventListener ("message", (event) => {
    const obj = JSON.parse (event.data);

    console.log (obj);
    const dropDown = document.getElementById ("getusers");
    if ("Users" in obj && obj.Type === 4 && obj.Users !== "")
    {
        const users = obj.Users;
        console.log (obj.Message);
        
        dropDown.innerHTML = '<option value="All users" onclick="displayMessageInput()">All users</option>';
        for (let i = 0; i < users.length; i++)
        {
            let option = document.createElement ("option");
            option.text = users [i];
            option.setAttribute ("onClick", "displayMessageInput()");
            dropDown.appendChild (option);
        }
    }
    else
    {
        appendMessage (event.data);
    }
});

function displayMessageInput ()
{
    const messageInput = document.getElementById ("messageInput");
    const sendButton = document.getElementById ("sendMessage");

    messageInput.style.display = "block";
    sendButton.style.display = "block";
}

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
    const sendTo = document.getElementById ("getusers");
    const user = sendTo.value;
    
    let request;

    if (user !== "All users")
    {
        request = {"Type": 3, "User": user, "Message": message};
    }
    else
    {
        request = {"Type": 2, "Message": message};
    }

    // Send the message to the server along with the username
    console.log (request);
    socket.send (JSON.stringify (request));
    messageInput.value = "";

    // document.getElementById ("getusers").setAttribute ("onclick", "getActiveUsers()");
    document.getElementById ("messageInput").style.display = "none";
    document.getElementById ("sendMessage").style.display = "none";
}

// function getActiveUsers () 
// {
//     const dropDown = document.getElementById ("getusers");
//     dropDown.removeAttribute ("onclick");

//     if (username === undefined)
//     {
//         addUsername ();
//         return;
//     }

//     const request = {"Type": 4};

//     console.log (request);
//     socket.send (JSON.stringify (request));
// }

function enableSend ()
{
    const message = document.getElementById ("messageInput");
    const button1 = document.getElementById ("sendName");
    const button2 = document.getElementById ("sendMessage");

    if (message.value.length > 0)
    {
        button1.disabled = false;
        button2.disabled = false;
    }
}

function displayNameInput ()
{
    const messageInput = document.getElementById ("messageInput");
    const sendButton = document.getElementById ("sendName");
    const button1 = document.getElementById ("sendName");
    const button2 = document.getElementById ("sendMessage");

    messageInput.style.display = "block";
    sendButton.style.display = "block";
    button1.disabled = true;
    button2.disabled = true;
}

function hideNameInput ()
{
    const messageInput = document.getElementById ("messageInput");
    const sendButton = document.getElementById ("sendName");

    messageInput.style.display = "none";
    sendButton.style.display = "none";
}

function updateName () 
{
    const messageInput = document.getElementById ("messageInput");
    const message = messageInput.value;

    const sendButton = document.getElementById ("sendName");
    messageInput.style.display = "block";
    sendButton.style.display = "block";

    if (message.length === 0) 
    {
        appendMessage ('{"Message": "Invalid name"}');
        messageInput.value = "";
        hideNameInput ();
        return;
    }

    const request = {"Type": 1, "Message": message};

    console.log (request)
    socket.send (JSON.stringify (request));
    messageInput.value = "";
    
    username = message;
    const welcomeMessage = document.getElementById ("welcomeMessage");
    welcomeMessage.innerHTML = "Welcome " + username;

    hideNameInput ();
}

function appendMessage (message) 
{
    const chatArea = document.getElementById ("chatArea");
    const messageElement = document.createElement ("div");

    chatArea.style.display = "block";
    messageElement.textContent = message;

    console.log (message);
    const obj = JSON.parse (message);
    if (obj.Message !== "Invalid name" && username === undefined)
    {
        message = "Please enter your name";
        messageElement.textContent = message;
    }
    else if (obj.Message === "Name already exists")
    {
        username = undefined;
        const welcomeMessage = document.getElementById ("welcomeMessage");
        welcomeMessage.innerHTML = "Enter valid username";

        messageElement.textContent = obj.Message;
    }
    else
    {
        let index;
        if (obj.Users !== undefined)
        {
            messageElement.textContent = obj.Users;
        }
        else
        {
            messageElement.textContent = obj.Message;
        }

        if (messageElement.textContent.length === 0)
            messageElement.textContent = "No active users!!!";
    }
    chatArea.appendChild (messageElement);
}
