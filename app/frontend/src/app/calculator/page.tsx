'use client'

import { useState } from 'react'
import { Card } from '@/components/ui/Card'
import { Input } from '@/components/ui/Input'
import { Button } from '@/components/ui/Button'
import Link from 'next/link'
import { ArrowLeft } from 'lucide-react'

export default function CalculatorPage() {
  const [length, setLength] = useState('')
  const [width, setWidth] = useState('')
  const [height, setHeight] = useState('2.7')
  const [results, setResults] = useState<any>(null)

  const calculate = () => {
    const l = parseFloat(length)
    const w = parseFloat(width)
    const h = parseFloat(height)

    if (!l || !w || !h) {
      alert('Заполните все поля')
      return
    }

    const floorArea = l * w
    const wallArea = 2 * (l + w) * h
    const volume = floorArea * h
    const ceilingArea = floorArea

    setResults({
      floorArea: floorArea.toFixed(2),
      wallArea: wallArea.toFixed(2),
      ceilingArea: ceilingArea.toFixed(2),
      volume: volume.toFixed(2),
      perimeter: (2 * (l + w)).toFixed(2),
    })
  }

  return (
    <div className="min-h-screen bg-gray-50">
      <header className="bg-white shadow-sm">
        <div className="max-w-7xl mx-auto px-4 py-4 sm:px-6 lg:px-8">
          <div className="flex items-center">
            <Link href="/" className="mr-4">
              <ArrowLeft className="w-6 h-6" />
            </Link>
            <h1 className="text-2xl font-bold text-gray-900">Калькулятор объемов</h1>
          </div>
        </div>
      </header>

      <main className="max-w-4xl mx-auto px-4 py-8 sm:px-6 lg:px-8">
        <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
          <Card title="Размеры помещения">
            <div className="space-y-4">
              <Input
                type="number"
                label="Длина (м)"
                placeholder="5.0"
                value={length}
                onChange={(e) => setLength(e.target.value)}
              />
              <Input
                type="number"
                label="Ширина (м)"
                placeholder="4.0"
                value={width}
                onChange={(e) => setWidth(e.target.value)}
              />
              <Input
                type="number"
                label="Высота (м)"
                placeholder="2.7"
                value={height}
                onChange={(e) => setHeight(e.target.value)}
              />
              <Button onClick={calculate} className="w-full">
                Рассчитать
              </Button>
            </div>
          </Card>

          {results && (
            <Card title="Результаты расчета">
              <div className="space-y-3">
                <div className="flex justify-between py-2 border-b">
                  <span className="font-medium">Площадь пола:</span>
                  <span>{results.floorArea} м²</span>
                </div>
                <div className="flex justify-between py-2 border-b">
                  <span className="font-medium">Площадь стен:</span>
                  <span>{results.wallArea} м²</span>
                </div>
                <div className="flex justify-between py-2 border-b">
                  <span className="font-medium">Площадь потолка:</span>
                  <span>{results.ceilingArea} м²</span>
                </div>
                <div className="flex justify-between py-2 border-b">
                  <span className="font-medium">Объем помещения:</span>
                  <span>{results.volume} м³</span>
                </div>
                <div className="flex justify-between py-2">
                  <span className="font-medium">Периметр:</span>
                  <span>{results.perimeter} м</span>
                </div>
              </div>
            </Card>
          )}
        </div>

        <div className="mt-8">
          <Card title="Примеры использования">
            <div className="space-y-2 text-sm text-gray-600">
              <p>• Штукатурка стен: используйте площадь стен</p>
              <p>• Стяжка пола: используйте площадь пола</p>
              <p>• Окраска потолка: используйте площадь потолка</p>
              <p>• Отопление: используйте объем помещения</p>
              <p>• Плинтус: используйте периметр</p>
            </div>
          </Card>
        </div>
      </main>
    </div>
  )
}
