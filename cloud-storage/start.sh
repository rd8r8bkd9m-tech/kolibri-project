#!/bin/bash

# Kolibri Cloud Storage API - Quick Start Guide

echo "╔════════════════════════════════════════╗"
echo "║  Kolibri Cloud Storage - Quick Start   ║"
echo "╚════════════════════════════════════════╝"
echo ""

cd "$(dirname "$0")"

# Check if node_modules exists
if [ ! -d "node_modules" ]; then
    echo "📦 Installing dependencies..."
    npm install
    echo ""
fi

echo "🚀 Starting Cloud Storage API..."
echo ""
echo "📝 API will be available at: http://localhost:3001"
echo ""
echo "📚 API Documentation:"
echo "   POST   /api/auth/register"
echo "   POST   /api/auth/login"
echo "   POST   /api/storage/upload"
echo "   GET    /api/storage/files"
echo "   GET    /api/storage/download/{fileId}"
echo "   DELETE /api/storage/files/{fileId}"
echo "   GET    /api/storage/info"
echo "   GET    /api/health"
echo ""
echo "🧪 To run tests in another terminal:"
echo "   npm run test"
echo ""
echo "💡 Tip: Use with Kolibri Web App to test file uploads"
echo ""

npm start
