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
      <button class="btn-primary" id="addInsult" command="show-modal" commandfor="my-dialog">Add Insult</button>

      <dialog id="addInsultDialog">
      <h2>Add Insult</h2>

      <form method="POST" id="addInsultForm">
        <input
          type="text"
          id="insultText"
          placeholder="Enter an insult..."
        />

        <button class="btn-secondary" type="button" id="cancelAddInsult">Cancel</button>
        <button id="submitInsult" class="btn-secondary" type="submit">Add Insult</button>
      </form>
    </dialog>
    </div>

    <ul>
    ${insults
      .map((insult) => {
        const id = insult.id;
        const text = insult.text;

        return `
  <li>
    <span style="display: inline-block; margin-bottom: 0.5em;">${text}</span>

    <div>
      <button id="editInsult" class="btn-secondary" data-id="${id}" data-action="edit">Edit</button>
      <button id="deleteInsult" class="btn-secondary" data-id="${id}" data-action="delete">Delete</button>
    </div>
  </li>
  `;
      })
      .join('')}
    </ul>
  </div>
`;
  const addInsultButton = document.getElementById('addInsult');
  const addInsultDialog = document.getElementById('addInsultDialog');
  const cancelAddInsult = document.getElementById('cancelAddInsult');
  const submitInsultButton = document.getElementById('submitInsultButton');

  const buttons = document.querySelectorAll('[data-action]');
  buttons.forEach((button) => {
    button.addEventListener('click', () => {
      const id = button.dataset.id;
      const action = button.dataset.action;

      console.log(id, action);
    });
  });

  addInsultButton.addEventListener('click', () => {
    addInsultDialog.showModal();
  });

  cancelAddInsult.addEventListener('click', () => {
    addInsultDialog.close();
  });

  submitInsultButton.addEventListener('submit', () => {});
  async function submitInsult() {
    
  }
}

async function showHome() {
  app.innerHTML = `<h1>Bard's Assistant</h1>`;
}
