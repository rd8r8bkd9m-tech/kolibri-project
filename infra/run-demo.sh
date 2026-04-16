#!/bin/bash

# ╔════════════════════════════════════════════════════════════════╗
# ║     Kolibri Complete Demo - Release v1.0.0                    ║
# ║     Author: Vladislav Evgenievich Kochurov                    ║
# ║     Description: Full integration demo with cloud storage      ║
# ╚════════════════════════════════════════════════════════════════╝

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLOUD_STORAGE_DIR="$PROJECT_ROOT/cloud-storage"
WEB_APP_DIR="$PROJECT_ROOT/frontend/kolibri-web"
RELEASE_LOG="$PROJECT_ROOT/RELEASE_v1.0.0_DEMO.log"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Functions
print_banner() {
  echo -e "${CYAN}"
  cat << "EOF"
╔════════════════════════════════════════════════════════════════╗
║                 🚀 KOLIBRI v1.0.0 DEMO                        ║
║                                                                ║
║         Complete integration with cloud storage and            ║
║             DECIMAL10X compression algorithm                   ║
╚════════════════════════════════════════════════════════════════╝
EOF
  echo -e "${NC}"
}

print_section() {
  echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
  echo -e "${BLUE}$1${NC}"
  echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

print_success() {
  echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
  echo -e "${RED}✗ $1${NC}"
}

print_info() {
  echo -e "${CYAN}ℹ $1${NC}"
}

# Main workflow
main() {
  print_banner
  
  {
    echo "Kolibri v1.0.0 Demo - $(date)"
    echo "=================================================="
  } | tee "$RELEASE_LOG"
  
  # Step 1: Check cloud storage server
  print_section "Step 1: Verifying Cloud Storage Server"
  
  if lsof -i :3001 &> /dev/null; then
    PID=$(lsof -i :3001 | grep LISTEN | awk '{print $2}' | head -1)
    print_success "Cloud Storage server is running (PID: $PID)"
    echo "Cloud Storage running on port 3001" >> "$RELEASE_LOG"
  else
    print_info "Starting Cloud Storage server..."
    cd "$CLOUD_STORAGE_DIR"
    nohup node server.js > "$CLOUD_STORAGE_DIR/server.log" 2>&1 &
    sleep 3
    
    if curl -s http://localhost:3001/api/health > /dev/null; then
      print_success "Cloud Storage server started successfully"
      echo "Cloud Storage started on port 3001" >> "$RELEASE_LOG"
    else
      print_error "Failed to start Cloud Storage server"
      exit 1
    fi
  fi
  
  # Step 2: Check web app server
  print_section "Step 2: Verifying Web Application"
  
  if lsof -i :5175 &> /dev/null; then
    print_success "Web App server is running"
    echo "Web App running on port 5175" >> "$RELEASE_LOG"
  else
    print_info "Starting Web App development server..."
    cd "$WEB_APP_DIR"
    npm run dev > "$WEB_APP_DIR/dev.log" 2>&1 &
    sleep 5
    
    if lsof -i :5175 &> /dev/null; then
      print_success "Web App started successfully"
      echo "Web App started on port 5175" >> "$RELEASE_LOG"
    else
      print_error "Failed to start Web App"
      exit 1
    fi
  fi
  
  # Step 3: Display URLs
  print_section "Step 3: Access Information"
  
  echo ""
  print_info "🌐 Web Application:"
  echo -e "   ${GREEN}http://localhost:5175${NC}"
  echo ""
  print_info "📦 Cloud Storage API:"
  echo -e "   ${GREEN}http://localhost:3001/api${NC}"
  echo ""
  print_info "📊 Storage Manager:"
  echo -e "   ${GREEN}http://localhost:5175/storage${NC}"
  echo ""
  
  # Step 4: Run tests
  print_section "Step 4: Running Integration Tests"
  
  echo ""
  read -p "$(echo -e "${YELLOW}Run cloud storage integration tests? (y/n): ${NC}")" -n 1 -r
  echo
  
  if [[ $REPLY =~ ^[Yy]$ ]]; then
    print_info "Running Node.js tests..."
    cd "$CLOUD_STORAGE_DIR"
    
    # Run Node tests
    timeout 60 npm run test 2>&1 | tee -a "$RELEASE_LOG" || true
    
    print_success "Test suite completed"
  fi
  
  # Step 5: Display summary
  print_section "Step 5: Demo Summary"
  
  echo ""
  echo -e "${CYAN}✨ Kolibri v1.0.0 is ready for demonstration!${NC}"
  echo ""
  echo -e "${YELLOW}Available Features:${NC}"
  echo "  📋 License Management - /licenses"
  echo "  💳 Payment Processing - /payments"
  echo "  ☁️  Cloud Storage - /storage (NEW!)"
  echo "  👤 User Profile - /profile"
  echo "  ⚙️  Settings - /settings"
  echo ""
  echo -e "${YELLOW}Kolibri Features:${NC}"
  echo "  🎯 DECIMAL10X compression algorithm"
  echo "  📊 Pattern detection and formula encoding"
  echo "  💾 Automatic data compression on upload"
  echo "  🔍 Compression statistics tracking"
  echo "  🚀 High-performance storage engine"
  echo ""
  echo -e "${YELLOW}Demo Workflow:${NC}"
  echo "  1. Open http://localhost:5175"
  echo "  2. Navigate to ☁️ Cloud Storage tab"
  echo "  3. Create account (any username/password)"
  echo "  4. Upload test files to see Kolibri compression"
  echo "  5. Download files to verify integrity"
  echo "  6. Check compression statistics"
  echo ""
  echo -e "${CYAN}🎉 Enjoy the demo!${NC}"
  echo ""
  
  # Step 6: Show logs
  print_section "Step 6: System Status"
  
  echo ""
  print_info "Cloud Storage Server Status:"
  if curl -s http://localhost:3001/api/health | grep -q "ok"; then
    echo -e "  ${GREEN}✓ Running${NC} - http://localhost:3001"
  else
    echo -e "  ${RED}✗ Not accessible${NC}"
  fi
  
  print_info "Web App Status:"
  if curl -s http://localhost:5175 > /dev/null 2>&1; then
    echo -e "  ${GREEN}✓ Running${NC} - http://localhost:5175"
  else
    echo -e "  ${RED}✗ Not accessible${NC}"
  fi
  
  echo ""
  print_info "Release log saved to: $RELEASE_LOG"
  
  # Step 7: Open browser
  print_section "Step 7: Launch Browser"
  
  echo ""
  read -p "$(echo -e "${YELLOW}Open web app in browser? (y/n): ${NC}")" -n 1 -r
  echo
  
  if [[ $REPLY =~ ^[Yy]$ ]]; then
    if command -v open &> /dev/null; then
      open http://localhost:5175
    elif command -v xdg-open &> /dev/null; then
      xdg-open http://localhost:5175
    elif command -v start &> /dev/null; then
      start http://localhost:5175
    else
      print_info "Please open http://localhost:5175 in your browser"
    fi
  fi
  
  echo ""
  print_section "Demo Ready!"
  
  {
    echo ""
    echo "Demo completed successfully at $(date)"
    echo "Web App: http://localhost:5175"
    echo "Cloud Storage: http://localhost:3001/api"
  } >> "$RELEASE_LOG"
}

# Run main
main "$@"
