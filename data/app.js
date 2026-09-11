const params = new URLSearchParams(window.location.search);
const page = params.get('page');
const app = document.getElementById('app');

// Only added for developing ease. Remove when ships.
const API_BASE = 'http://bardsassistant.local';

if (page === 'insults') {
  showInsults();
} else {
  showHome();
}

/**
 * Displays the insults deck and provides controls for adding, editing, and deleting insults.
 */
async function showInsults() {
  const response = await fetch(`${API_BASE}/api/decks?id=insults`);
  const insults = await response.json();

  app.innerHTML = `
  <div class="card">
    <div class="card-header">
      <h2>Insults</h2>
      <button class="btn-primary" id="addInsult">Add Insult</button>
    </div>

    <ul id="insultList"></ul>
  </div>

  <dialog id="addInsultDialog">
    <h2>Add Insult</h2>
    <form method="POST" id="addInsultForm">
      <div>
        <input type="text" id="insultText" placeholder="Enter an insult..." />
      </div>
      <div class="dialog-actions">
        <button class="btn-secondary" type="button" id="cancelAddInsult">Cancel</button>
        <button id="submitInsult" class="btn-primary" type="submit">Add</button>
      </div>
    </form>
  </dialog>

  <dialog id="editInsultDialog">
    <h2>Edit Insult</h2>
    <form id="editInsultForm">
      <div>
        <input type="text" id="editInsultText" placeholder="Enter an insult..." />
      </div>
      <div class="dialog-actions">
        <button class="btn-secondary" type="button" id="cancelEditInsult">Cancel</button>
        <button class="btn-primary" type="submit">Save</button>
      </div>
    </form>
  </dialog>

  <dialog id="deleteInsultDialog">
    <h2>Delete Insult</h2>
    <p id="deleteInsultPreview"></p>
    <p>This action cannot be undone.</p>
    <div class="dialog-actions">
      <button class="btn-secondary" type="button" id="cancelDeleteInsult">Cancel</button>
      <button class="btn-primary" type="button" id="confirmDeleteInsult">Delete</button>
    </div>
  </dialog>
`;
  const addInsultButton = document.getElementById('addInsult');
  const addInsultDialog = document.getElementById('addInsultDialog');
  const cancelAddInsult = document.getElementById('cancelAddInsult');
  const submitInsultForm = document.getElementById('addInsultForm');
  const editInsultDialog = document.getElementById('editInsultDialog');
  const editInsultText = document.getElementById('editInsultText');
  const cancelEditInsult = document.getElementById('cancelEditInsult');
  const editInsultForm = document.getElementById('editInsultForm');
  const deleteInsultDialog = document.getElementById('deleteInsultDialog');
  const deleteInsultPreview = document.getElementById('deleteInsultPreview');
  const cancelDeleteInsult = document.getElementById('cancelDeleteInsult');
  const confirmDeleteInsult = document.getElementById('confirmDeleteInsult');
  const insultList = document.getElementById('insultList');

  let editingId = null;
  let deletingId = null;

  cancelDeleteInsult.addEventListener('click', () => {
    deleteInsultDialog.close();
  });

  confirmDeleteInsult.addEventListener('click', async () => {
    try {
      const response = await fetch(`${API_BASE}/api/decks?id=insults&entryId=${deletingId}`, {
        method: 'DELETE',
      });
      if (!response.ok) throw new Error(`HTTP error! Status: ${response.status}`);
      deleteInsultDialog.close();
      await showInsults();
    } catch (error) {
      console.error('Failed to delete insult:', error);
    }
  });

  cancelEditInsult.addEventListener('click', () => {
    editInsultDialog.close();
  });

  editInsultForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    const text = editInsultText.value;
    if (!text.trim()) return;
    try {
      const response = await fetch(
        `${API_BASE}/api/decks?id=insults&entryId=${editingId}`,
        {
          method: 'PUT',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ text }),
        }
      );
      if (!response.ok) throw new Error(`HTTP error! Status: ${response.status}`);
      editInsultDialog.close();
      await showInsults();
    } catch (error) {
      console.error('Failed to edit insult:', error);
    }
  });

  if (insults.length === 0) {
    const empty = document.createElement('li');
    empty.textContent = 'No insults yet. Add one to get started.';
    empty.style.color = '#888';
    empty.style.fontStyle = 'italic';
    insultList.appendChild(empty);
  }

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

    editBtn.addEventListener('click', () => {
      editingId = insult.id;
      editInsultText.value = insult.text;
      editInsultDialog.showModal();
    });

    deleteBtn.addEventListener('click', () => {
      deletingId = insult.id;
      deleteInsultPreview.textContent = insult.text;
      deleteInsultDialog.showModal();
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
      await showInsults();
    } catch (error) {
      console.error('Failed to submit insult:', error);
    }
  });
}

/**
 * Displays the application's home page.
 */
async function showHome() {
  app.innerHTML = `<h1>Bard's Assistant</h1>`;
}
