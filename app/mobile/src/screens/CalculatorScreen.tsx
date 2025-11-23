import React, { useState } from 'react';
import { View, StyleSheet, ScrollView } from 'react-native';
import { Card, Title, TextInput, Button, DataTable } from 'react-native-paper';

export default function CalculatorScreen() {
  const [length, setLength] = useState('');
  const [width, setWidth] = useState('');
  const [height, setHeight] = useState('2.7');
  const [results, setResults] = useState(null);

  const calculate = () => {
    const l = parseFloat(length);
    const w = parseFloat(width);
    const h = parseFloat(height);

    if (!l || !w || !h) {
      alert('Заполните все поля');
      return;
    }

    const floorArea = l * w;
    const wallArea = 2 * (l + w) * h;
    const volume = floorArea * h;
    const ceilingArea = floorArea;
    const perimeter = 2 * (l + w);

    setResults({
      floorArea: floorArea.toFixed(2),
      wallArea: wallArea.toFixed(2),
      ceilingArea: ceilingArea.toFixed(2),
      volume: volume.toFixed(2),
      perimeter: perimeter.toFixed(2),
    });
  };

  return (
    <ScrollView style={styles.container}>
      <Card style={styles.card}>
        <Card.Content>
          <Title>Размеры помещения</Title>
          <TextInput
            label="Длина (м)"
            value={length}
            onChangeText={setLength}
            keyboardType="numeric"
            style={styles.input}
          />
          <TextInput
            label="Ширина (м)"
            value={width}
            onChangeText={setWidth}
            keyboardType="numeric"
            style={styles.input}
          />
          <TextInput
            label="Высота (м)"
            value={height}
            onChangeText={setHeight}
            keyboardType="numeric"
            style={styles.input}
          />
          <Button mode="contained" onPress={calculate} style={styles.button}>
            Рассчитать
          </Button>
        </Card.Content>
      </Card>

      {results && (
        <Card style={styles.card}>
          <Card.Content>
            <Title>Результаты</Title>
            <DataTable>
              <DataTable.Row>
                <DataTable.Cell>Площадь пола</DataTable.Cell>
                <DataTable.Cell numeric>{results.floorArea} м²</DataTable.Cell>
              </DataTable.Row>
              <DataTable.Row>
                <DataTable.Cell>Площадь стен</DataTable.Cell>
                <DataTable.Cell numeric>{results.wallArea} м²</DataTable.Cell>
              </DataTable.Row>
              <DataTable.Row>
                <DataTable.Cell>Площадь потолка</DataTable.Cell>
                <DataTable.Cell numeric>{results.ceilingArea} м²</DataTable.Cell>
              </DataTable.Row>
              <DataTable.Row>
                <DataTable.Cell>Объем помещения</DataTable.Cell>
                <DataTable.Cell numeric>{results.volume} м³</DataTable.Cell>
              </DataTable.Row>
              <DataTable.Row>
                <DataTable.Cell>Периметр</DataTable.Cell>
                <DataTable.Cell numeric>{results.perimeter} м</DataTable.Cell>
              </DataTable.Row>
            </DataTable>
          </Card.Content>
        </Card>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#f5f5f5',
  },
  card: {
    margin: 10,
  },
  input: {
    marginBottom: 10,
  },
  button: {
    marginTop: 10,
  },
});
