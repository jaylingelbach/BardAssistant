const params = new URLSearchParams(window.location.search);
const page = params.get('page');
const app = document.getElementById('app');

const API_BASE = 'http://bardsassistant.local';

if (page === 'insults') {
  showInsults();
} else {
  showHome();
}

async function showInsults() {
  const response = await fetch(`${API_BASE}/api/decks?id=insults`);
  const insults = await response.json();

  app.innerHTML = `
  <div class="card">
    <div class="card-header">
      <h2>Insults</h2>
      <button class="btn-primary" id="addInsult">Add Insult</button>

      <dialog id="addInsultDialog">
      <h2>Add Insult</h2>

      <form method="POST" id="addInsultForm">
      <div>
        <input
          type="text"
          id="insultText"
          placeholder="Enter an insult..."
        />
      </div>
        <div class="dialog-actions">
          <button class="btn-secondary" type="button" id="cancelAddInsult">Cancel</button>
          <button id="submitInsult" class="btn-primary" type="submit">Add</button>
        </div>
      </form>
    </dialog>
    </div>

    <ul id="insultList"></ul>
  </div>
`;
  const addInsultButton = document.getElementById('addInsult');
  const addInsultDialog = document.getElementById('addInsultDialog');
  const cancelAddInsult = document.getElementById('cancelAddInsult');
  const submitInsultForm = document.getElementById('addInsultForm');
  const insultList = document.getElementById('insultList');

  insults.forEach((insult) => {
    const li = document.createElement('li');

    const span = document.createElement('span');
    span.style.display = 'inline-block';
    span.style.marginBottom = '0.5em';
    span.textContent = insult.text;

    const actions = document.createElement('div');

    const editBtn = document.createElement('button');
    editBtn.className = 'btn-small';
    editBtn.dataset.id = insult.id;
    editBtn.dataset.action = 'edit';
    editBtn.textContent = 'Edit';

    const deleteBtn = document.createElement('button');
    deleteBtn.className = 'btn-small';
    deleteBtn.dataset.id = insult.id;
    deleteBtn.dataset.action = 'delete';
    deleteBtn.textContent = 'Delete';

    [editBtn, deleteBtn].forEach((btn) => {
      btn.addEventListener('click', () => {
        console.log(btn.dataset.id, btn.dataset.action);
      });
    });

    actions.appendChild(editBtn);
    actions.appendChild(deleteBtn);
    li.appendChild(span);
    li.appendChild(actions);
    insultList.appendChild(li);
  });

  addInsultButton.addEventListener('click', () => {
    addInsultDialog.showModal();
  });

  cancelAddInsult.addEventListener('click', () => {
    addInsultDialog.close();
  });

  submitInsultForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    const url = `${API_BASE}/api/decks?id=insults`;
    const inputElement = document.getElementById('insultText');
    const userInsult = inputElement.value;

    if (!userInsult.trim()) {
      console.warn('Input is empty.');
      return;
    }
    const payload = {
      text: userInsult,
    };
    try {
      const response = await fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json', // Instructs the server to parse the body as JSON
        },
        body: JSON.stringify(payload),
      });
      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }
      await response.text();
      inputElement.value = '';
      addInsultDialog.close();
    } catch (error) {
      console.error('Failed to submit insult:', error);
    }
  });
}

async function showHome() {
  app.innerHTML = `<h1>Bard's Assistant</h1>`;
}
