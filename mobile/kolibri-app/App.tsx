import React from 'react';
import { NavigationContainer } from '@react-navigation/native';
import { createNativeStackNavigator } from '@react-navigation/stack';
import { createBottomTabNavigator } from '@react-navigation/bottom-tabs';
import { StatusBar } from 'expo-status-bar';
import { SafeAreaProvider } from 'react-native-safe-area-context';

// Screens
import SplashScreen from './src/screens/SplashScreen';
import LoginScreen from './src/screens/auth/LoginScreen';
import RegisterScreen from './src/screens/auth/RegisterScreen';
import DashboardScreen from './src/screens/dashboard/DashboardScreen';
import LicensesScreen from './src/screens/licenses/LicensesScreen';
import LicenseDetailScreen from './src/screens/licenses/LicenseDetailScreen';
import PaymentsScreen from './src/screens/payments/PaymentsScreen';
import ProfileScreen from './src/screens/profile/ProfileScreen';
import SettingsScreen from './src/screens/settings/SettingsScreen';

// Navigation
const Stack = createNativeStackNavigator();
const Tab = createBottomTabNavigator();

// Auth Navigation
function AuthStack() {
  return (
    <Stack.Navigator
      screenOptions={{
        headerShown: false,
        animationEnabled: true,
      }}
    >
      <Stack.Screen name="Login" component={LoginScreen} />
      <Stack.Screen name="Register" component={RegisterScreen} />
    </Stack.Navigator>
  );
}

// App Navigation
function AppTabs() {
  return (
    <Tab.Navigator
      screenOptions={{
        headerShown: true,
        tabBarActiveTintColor: '#00d4ff',
        tabBarInactiveTintColor: '#666666',
        tabBarStyle: {
          backgroundColor: '#0f0f0f',
          borderTopColor: '#1a1a2e',
          borderTopWidth: 1,
        },
      }}
    >
      <Tab.Screen 
        name="Dashboard" 
        component={DashboardScreen}
        options={{
          title: 'Главная',
          tabBarLabel: 'Главная',
        }}
      />
      <Tab.Screen 
        name="Licenses" 
        component={LicensesScreen}
        options={{
          title: 'Лицензии',
          tabBarLabel: 'Лицензии',
        }}
      />
      <Tab.Screen 
        name="Payments" 
        component={PaymentsScreen}
        options={{
          title: 'Платежи',
          tabBarLabel: 'Платежи',
        }}
      />
      <Tab.Screen 
        name="Profile" 
        component={ProfileScreen}
        options={{
          title: 'Профиль',
          tabBarLabel: 'Профиль',
        }}
      />
    </Tab.Navigator>
  );
}

function AppStack() {
  return (
    <Stack.Navigator
      screenOptions={{
        headerShown: false,
      }}
    >
      <Stack.Screen 
        name="AppTabs" 
        component={AppTabs} 
        options={{ headerShown: false }}
      />
      <Stack.Screen 
        name="LicenseDetail" 
        component={LicenseDetailScreen}
        options={{
          title: 'Детали лицензии',
          headerShown: true,
        }}
      />
      <Stack.Screen 
        name="Settings" 
        component={SettingsScreen}
        options={{
          title: 'Настройки',
          headerShown: true,
        }}
      />
    </Stack.Navigator>
  );
}

// Root Navigator
function RootNavigator({ isLoading, userToken }) {
  if (isLoading) {
    return <SplashScreen />;
  }

  return (
    <NavigationContainer>
      {userToken == null ? <AuthStack /> : <AppStack />}
    </NavigationContainer>
  );
}

export default function App() {
  const [isLoading, setIsLoading] = React.useState(true);
  const [userToken, setUserToken] = React.useState(null);

  React.useEffect(() => {
    // Симуляция загрузки
    const timeout = setTimeout(() => {
      setIsLoading(false);
    }, 2000);

    return () => clearTimeout(timeout);
  }, []);

  return (
    <SafeAreaProvider>
      <RootNavigator isLoading={isLoading} userToken={userToken} />
      <StatusBar barStyle="light-content" />
    </SafeAreaProvider>
  );
}
