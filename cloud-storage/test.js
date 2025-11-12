import fs from 'fs/promises';

const API_URL = 'http://localhost:3001/api';
let authToken = '';

async function test(name, fn) {
  try {
    console.log(`\n🧪 Testing: ${name}`);
    await fn();
    console.log(`   ✅ Passed`);
  } catch (err) {
    console.error(`   ❌ Failed: ${err.message}`);
  }
}

async function post(endpoint, data) {
  const response = await fetch(`${API_URL}${endpoint}`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      ...(authToken && { 'Authorization': `Bearer ${authToken}` })
    },
    body: JSON.stringify(data)
  });
  
  if (!response.ok) {
    const error = await response.json();
    throw new Error(`${response.status}: ${error.error}`);
  }
  
  return response.json();
}

async function get(endpoint) {
  const response = await fetch(`${API_URL}${endpoint}`, {
    headers: {
      ...(authToken && { 'Authorization': `Bearer ${authToken}` })
    }
  });
  
  if (!response.ok) {
    const error = await response.json();
    throw new Error(`${response.status}: ${error.error}`);
  }
  
  return response.json();
}

async function deleteFile(endpoint) {
  const response = await fetch(`${API_URL}${endpoint}`, {
    method: 'DELETE',
    headers: {
      ...(authToken && { 'Authorization': `Bearer ${authToken}` })
    }
  });
  
  if (!response.ok) {
    const error = await response.json();
    throw new Error(`${response.status}: ${error.error}`);
  }
  
  return response.json();
}

async function uploadFile(filename, content) {
  // Create test file
  await fs.writeFile(`/tmp/${filename}`, content);
  
  const formData = new FormData();
  const fileBlob = new Blob([content], { type: 'text/plain' });
  formData.append('file', fileBlob, filename);
  
  const response = await fetch(`${API_URL}/storage/upload`, {
    method: 'POST',
    headers: {
      'Authorization': `Bearer ${authToken}`
    },
    body: formData
  });
  
  if (!response.ok) {
    const error = await response.json();
    throw new Error(`${response.status}: ${error.error}`);
  }
  
  return response.json();
}

async function runTests() {
  console.log(`
╔════════════════════════════════════════╗
║   Kolibri Cloud Storage - Test Suite   ║
╚════════════════════════════════════════╝
  `);
  
  // Test: Register
  await test('User Registration', async () => {
    const result = await post('/auth/register', {
      username: 'testuser',
      password: 'password123'
    });
    
    authToken = result.token;
    
    if (!result.user.id || !result.token) {
      throw new Error('Invalid response');
    }
    console.log(`   User ID: ${result.user.id}`);
    console.log(`   Storage: ${result.storage.limit} bytes`);
  });
  
  // Test: Login
  await test('User Login', async () => {
    const result = await post('/auth/login', {
      username: 'testuser',
      password: 'password123'
    });
    
    authToken = result.token;
    
    if (!result.token) {
      throw new Error('Invalid token');
    }
    console.log(`   Token received: ${result.token.substring(0, 20)}...`);
  });
  
  // Test: Get storage info
  await test('Get Storage Info', async () => {
    const result = await get('/storage/info');
    
    if (!result.storage) {
      throw new Error('Invalid response');
    }
    console.log(`   Used: ${result.storage.used} bytes`);
    console.log(`   Limit: ${result.storage.limit} bytes`);
    console.log(`   Files: ${result.filesCount}`);
  });
  
  // Test: List files (should be empty)
  await test('List Files (Empty)', async () => {
    const result = await get('/storage/files');
    
    if (result.total !== 0) {
      throw new Error('Should have no files');
    }
    console.log(`   Files count: ${result.total}`);
  });
  
  // Test: Upload file
  let fileId = '';
  await test('Upload File', async () => {
    const result = await uploadFile('test-file.txt', 'Hello, Cloud Storage! 🚀');
    
    fileId = result.file.id;
    
    if (!fileId) {
      throw new Error('No file ID returned');
    }
    console.log(`   File ID: ${fileId}`);
    console.log(`   Size: ${result.file.size} bytes`);
    console.log(`   Storage used: ${result.storage.used} bytes`);
  });
  
  // Test: List files (should have 1)
  await test('List Files (With File)', async () => {
    const result = await get('/storage/files');
    
    if (result.total !== 1) {
      throw new Error('Should have 1 file');
    }
    console.log(`   Files count: ${result.total}`);
    console.log(`   First file: ${result.files[0].name}`);
  });
  
  // Test: Download file
  await test('Download File', async () => {
    const response = await fetch(`${API_URL}/storage/download/${fileId}`, {
      headers: {
        'Authorization': `Bearer ${authToken}`
      }
    });
    
    if (!response.ok) {
      throw new Error('Download failed');
    }
    
    const content = await response.text();
    console.log(`   Downloaded: ${content}`);
  });
  
  // Test: Delete file
  await test('Delete File', async () => {
    const result = await deleteFile(`/storage/files/${fileId}`);
    
    console.log(`   Message: ${result.message}`);
    console.log(`   Storage used after delete: ${result.storage.used} bytes`);
  });
  
  // Test: List files (should be empty again)
  await test('List Files (After Delete)', async () => {
    const result = await get('/storage/files');
    
    if (result.total !== 0) {
      throw new Error('Should have no files');
    }
    console.log(`   Files count: ${result.total}`);
  });
  
  // Test: Health check
  await test('Health Check', async () => {
    const result = await fetch(`${API_URL}/health`).then(r => r.json());
    
    if (result.status !== 'ok') {
      throw new Error('Health check failed');
    }
    console.log(`   Status: ${result.status}`);
    console.log(`   Service: ${result.service}`);
  });
  
  console.log(`
╔════════════════════════════════════════╗
║   ✅ All tests completed!              ║
╚════════════════════════════════════════╝
  `);
}

// Run tests
runTests().catch(err => {
  console.error('Test suite failed:', err);
  process.exit(1);
});
