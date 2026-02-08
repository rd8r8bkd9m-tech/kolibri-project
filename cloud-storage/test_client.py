#!/usr/bin/env python3
"""
Kolibri Cloud Storage API - Python Test Client
Tests file upload, download, and management
"""

import requests
import time
from pathlib import Path

API_URL = "http://localhost:3001/api"
TOKEN = None

def register_user(username="kolibri_user", password="secure_pass"):
    """Register new user"""
    print(f"\n🔐 Registering user: {username}")
    response = requests.post(
        f"{API_URL}/auth/register",
        json={"username": username, "password": password}
    )
    
    if response.status_code == 201:
        data = response.json()
        print("✅ User registered successfully")
        print(f"   ID: {data['user']['id']}")
        print(f"   Storage: {data['storage']['limit'] / (1024**3):.1f} GB")
        return data['token']
    elif response.status_code == 409:
        print("⚠️  User already exists, logging in...")
        return login_user(username, password)
    else:
        print(f"❌ Error: {response.json()}")
        return None

def login_user(username, password):
    """Login user"""
    print(f"\n🔓 Logging in: {username}")
    response = requests.post(
        f"{API_URL}/auth/login",
        json={"username": username, "password": password}
    )
    
    if response.status_code == 200:
        data = response.json()
        print("✅ Logged in successfully")
        return data['token']
    else:
        print(f"❌ Error: {response.json()}")
        return None

def get_storage_info(token):
    """Get storage information"""
    print("\n📊 Storage Information")
    headers = {"Authorization": f"Bearer {token}"}
    response = requests.get(f"{API_URL}/storage/info", headers=headers)
    
    if response.status_code == 200:
        data = response.json()
        used_gb = data['storage']['used'] / (1024**3)
        limit_gb = data['storage']['limit'] / (1024**3)
        print("✅ Storage info retrieved")
        print(f"   Used: {used_gb:.2f} GB / {limit_gb:.2f} GB")
        print(f"   Files: {data['filesCount']}")
        print(f"   Usage: {data['usagePercent']}%")
        return data
    else:
        print(f"❌ Error: {response.json()}")
        return None

def upload_file(token, file_path):
    """Upload file"""
    if not Path(file_path).exists():
        print(f"\n❌ File not found: {file_path}")
        return None
    
    print(f"\n📤 Uploading file: {file_path}")
    headers = {"Authorization": f"Bearer {token}"}
    
    with open(file_path, 'rb') as f:
        files = {'file': f}
        response = requests.post(
            f"{API_URL}/storage/upload",
            headers=headers,
            files=files
        )
    
    if response.status_code == 201:
        data = response.json()
        size_kb = data['file']['size'] / 1024
        print("✅ File uploaded successfully")
        print(f"   File ID: {data['file']['id']}")
        print(f"   Size: {size_kb:.1f} KB")
        print(f"   Type: {data['file']['mimeType']}")
        return data['file']['id']
    else:
        print(f"❌ Error: {response.json()}")
        return None

def list_files(token):
    """List all files"""
    print("\n📋 File List")
    headers = {"Authorization": f"Bearer {token}"}
    response = requests.get(f"{API_URL}/storage/files", headers=headers)
    
    if response.status_code == 200:
        data = response.json()
        print(f"✅ Retrieved {data['total']} files:")
        for i, file in enumerate(data['files'], 1):
            size_kb = file['size'] / 1024
            print(f"   {i}. {file['name']} ({size_kb:.1f} KB)")
        return data['files']
    else:
        print(f"❌ Error: {response.json()}")
        return []

def download_file(token, file_id, output_path):
    """Download file"""
    print(f"\n📥 Downloading file: {file_id}")
    headers = {"Authorization": f"Bearer {token}"}
    response = requests.get(
        f"{API_URL}/storage/download/{file_id}",
        headers=headers
    )
    
    if response.status_code == 200:
        with open(output_path, 'wb') as f:
            f.write(response.content)
        print("✅ File downloaded successfully")
        print(f"   Saved to: {output_path}")
        return True
    else:
        print(f"❌ Error: {response.json()}")
        return False

def delete_file(token, file_id):
    """Delete file"""
    print(f"\n🗑️  Deleting file: {file_id}")
    headers = {"Authorization": f"Bearer {token}"}
    response = requests.delete(
        f"{API_URL}/storage/files/{file_id}",
        headers=headers
    )
    
    if response.status_code == 200:
        response.json()
        print("✅ File deleted successfully")
        return True
    else:
        print(f"❌ Error: {response.json()}")
        return False

def health_check():
    """Check API health"""
    print("\n🏥 Health Check")
    response = requests.get(f"{API_URL}/health")
    
    if response.status_code == 200:
        data = response.json()
        print("✅ Service is up")
        print(f"   Status: {data['status']}")
        print(f"   Service: {data['service']}")
        return True
    else:
        print("❌ Service is down")
        return False

def main():
    """Main test sequence"""
    print("""
╔════════════════════════════════════════╗
║  Kolibri Cloud Storage - Python Test   ║
╚════════════════════════════════════════╝
    """)
    
    # Health check
    if not health_check():
        print("\n❌ Cloud Storage API is not running!")
        print("   Start it with: cd cloud-storage && node server.js")
        return
    
    # Register/Login
    token = register_user()
    if not token:
        return
    
    time.sleep(0.5)
    
    # Get storage info
    get_storage_info(token)
    
    time.sleep(0.5)
    
    # Create test file
    test_file = "/tmp/test_document.txt"
    with open(test_file, 'w') as f:
        f.write("""
Kolibri Cloud Storage Test
===========================

This is a test file for cloud storage testing.
You can upload any type of file (images, documents, etc.)

Features:
- User authentication with JWT
- File upload and download
- Storage management
- File listing
- File deletion

Author: Vladislav Evgenievich Kochurov
Website: https://kolibriai.ru
        """)
    
    # Upload file
    file_id = upload_file(token, test_file)
    if not file_id:
        return
    
    time.sleep(0.5)
    
    # List files
    list_files(token)
    
    time.sleep(0.5)
    
    # Download file
    if file_id:
        output_file = f"/tmp/downloaded_{file_id[:8]}.txt"
        download_file(token, file_id, output_file)
        time.sleep(0.5)
    
    # Get storage info after upload
    get_storage_info(token)
    
    time.sleep(0.5)
    
    # Delete file
    if file_id:
        delete_file(token, file_id)
        time.sleep(0.5)
    
    # Final storage info
    get_storage_info(token)
    
    print(f"""
╔════════════════════════════════════════╗
║  ✅ All tests completed!               ║
╚════════════════════════════════════════╝

📚 Next steps:
   - Integrate with Kolibri Web App
   - Add more file types
   - Test with larger files
   - Deploy to production

📖 API Documentation:
   GET  /api/health
   POST /api/auth/register
   POST /api/auth/login
   POST /api/storage/upload
   GET  /api/storage/files
   GET  /api/storage/download/{fileId}
   DELETE /api/storage/files/{fileId}
   GET  /api/storage/info

🌐 Access the API at: http://localhost:3001
    """)

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()
